.. _doc_manual_multiplayer_tree:

The MultiplayerTree
===================

The :ref:`MultiplayerTree <class_MultiplayerTree>` is the central node of every
Networked session. It owns a :godot:`SceneMultiplayer <SceneMultiplayer>`,
mounts it on its own path in the :godot:`SceneTree <SceneTree>`, talks to a
:ref:`BackendPeer <class_BackendPeer>` for transport, and coordinates the
session-wide flow: who is hosting, who is joining, who has authenticated, and
which players have been accepted into the world.

Most user code never instantiates the tree manually. You add it as a node in
the editor, fill in its inspector fields, and then either let it connect on
:godot:`_ready() <Node#class_node_private_method__ready>` (via :button:`Init Join Payload`) or drive it from a script with
:ref:`connect_player() <class_MultiplayerTree_method_connect_player>`. This
page describes the lifecycle, the role and state machine, custom transports,
and embedded servers.

A tree in the scene
-------------------

The tree is a plain :godot:`Node <Node>`, so it follows the normal scene-tree
rules: it must be added before children that depend on the multiplayer API
are themselves added. That happens for free in editor-authored scenes
because Godot calls :godot:`_enter_tree() <Node#class_node_private_method__enter_tree>` top-down, and the tree mounts its
:godot:`SceneMultiplayer <SceneMultiplayer>` inside its own :godot:`_enter_tree() <Node#class_node_private_method__enter_tree>`.
Children running :godot:`_ready() <Node#class_node_private_method__ready>` later can safely read :godot:`multiplayer <Node#class_node_property_multiplayer>` and get the
tree-owned API, not the global default.

Two tree placements are common:

- A single tree at the top of the gameplay scene. This is what the quick
  start uses. The tree hosts or joins, and child level scenes are spawned
  beneath it.
- A ``Client`` tree with an automatically-spawned sibling ``Server`` tree. When
  :ref:`connect_player() <class_MultiplayerTree_method_connect_player>` is
  called with a local URL on a backend that supports embedded servers, the
  tree duplicates itself, names the copy ``Server``, calls :ref:`host() <class_MultiplayerTree_method_host>` on it,
  and then calls :ref:`join() <class_MultiplayerTree_method_join>` on the original. Both trees end up under the
  same parent and share no state beyond the loopback transport. This is the
  default for in-editor playtesting on desktop backends.

If your backend supports it and you would rather not pay for the duplicate
node, set :button:`Use Listen Server` on the tree. The same node will accept
remote peers and represent the local host player at the same time.

.. note::

    Listen-server mode is opt-in for now. The reason is historical. The
    duplicate-tree layout was the default for a long time, and several
    examples still rely on it. The flag will eventually flip to default-on
    once the example projects have migrated.

Roles and states
----------------

A tree always exposes two enums that together describe what it is doing
right now: :ref:`role <class_MultiplayerTree_property_role>` and
:ref:`state <class_MultiplayerTree_property_state>`.

The role tells you *what* this tree is in the session. The four possible
values are:

- :ref:`NONE <class_MultiplayerTree_constant_NONE>`: no transport assigned yet, or the session has been torn down.
- :ref:`CLIENT <class_MultiplayerTree_constant_CLIENT>`: this tree has joined a remote server.
- :ref:`DEDICATED_SERVER <class_MultiplayerTree_constant_DEDICATED_SERVER>`: this tree is hosting and is **not** also a player.
- :ref:`LISTEN_SERVER <class_MultiplayerTree_constant_LISTEN_SERVER>`: this tree is hosting and is also a local player.

The role is decided during :ref:`host() <class_MultiplayerTree_method_host>` / :ref:`join() <class_MultiplayerTree_method_join>` / :ref:`adopt_peer() <class_MultiplayerTree_method_adopt_peer>` and
does not change for the lifetime of the session. The convenience properties
:ref:`is_host <class_MultiplayerTree_property_is_host>` and
:ref:`is_local_client <class_MultiplayerTree_property_is_local_client>` cover
the typical branches without forcing you to remember which combinations map
to which role.

The state tells you *where in the lifecycle* the tree is right now: it
moves from :ref:`OFFLINE <class_MultiplayerTree_constant_OFFLINE>` to :ref:`CONNECTING <class_MultiplayerTree_constant_CONNECTING>` while the backend sets up, to
:ref:`ONLINE <class_MultiplayerTree_constant_ONLINE>` once the peer is live and authentication has settled, and back
through :ref:`DISCONNECTING <class_MultiplayerTree_constant_DISCONNECTING>` on shutdown. The
:ref:`state_changed <class_MultiplayerTree_signal_state_changed>` signal
fires on every transition, so UI elements that want to display "Connecting…"
or grey out a *Disconnect* button can subscribe once and be done.

.. warning::

    Reading :ref:`is_host <class_MultiplayerTree_property_is_host>` or :ref:`is_local_client <class_MultiplayerTree_property_is_local_client>` before the tree has finished
    configuring is a programmer error. The role is still :ref:`NONE <class_MultiplayerTree_constant_NONE>` and the
    addon will log a warning. Connect to the
    :ref:`configured <class_MultiplayerTree_signal_configured>` signal
    instead, or guard the read with :ref:`state <class_MultiplayerTree_property_state>` == :ref:`ONLINE <class_MultiplayerTree_constant_ONLINE>`.

The connection flow
-------------------

There are four entry methods, each for a different intent. All take a
:ref:`JoinPayload <class_JoinPayload>` describing the player; transport
identity (backend, address) is passed separately so the payload carries no
URL or transport-specific fields.

- :ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`:
  query the address, join if a live server answers, otherwise host. The
  zero-config path for local development and listen-server games.
- :ref:`join_direct() <class_MultiplayerTree_method_join_direct>`: open
  the configured backend against a known address as a client. Use when the
  caller knows there is a server.
- :ref:`host_player() <class_MultiplayerTree_method_host_player>`: start
  this tree as the host. Use when the caller knows it is hosting.
- :ref:`adopt_peer() <class_MultiplayerTree_method_adopt_peer>`: attach a
  pre-connected :godot:`MultiplayerPeer <MultiplayerPeer>` produced by an
  external system (Steam lobby, matchmaker) without going through a
  :ref:`BackendPeer <class_BackendPeer>`.

A typical local flow looks like this:

1. Validate the payload (username non-empty, spawner path resolvable).
2. Run the auth pipeline on the payload, if an
   :ref:`auth_provider <class_MultiplayerTree_property_auth_provider>` is
   assigned.
3. (auto-connect only) call
   :ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
   against the address. A reply with ``is_local_listener = true`` means
   join; anything else means host.
4. After the transport hand-off, send the resolved payload to the server
   via :ref:`submit_join() <class_MultiplayerTree_method_submit_join>`.
5. The server validates, resolves identity, broadcasts the accepted player
   to all peers, and emits
   :ref:`player_joined <class_MultiplayerTree_signal_player_joined>`
   everywhere, including locally on the new peer.

For the protocol behind ``query_server_info`` and probe isolation, see
:doc:`pre_game_connection`.

If the peer is produced by an external system (a Steam lobby, a matchmaking
service) and is already connected by the time you get it, hand it directly
to :ref:`adopt_peer() <class_MultiplayerTree_method_adopt_peer>`. The tree
will skip the backend setup, plug the peer into its
:godot:`SceneMultiplayer <SceneMultiplayer>`, and finalize as a :ref:`CLIENT <class_MultiplayerTree_constant_CLIENT>`
or :ref:`LISTEN_SERVER <class_MultiplayerTree_constant_LISTEN_SERVER>` based on the peer's unique ID.

Signals you will actually wire
------------------------------

The tree exposes a generous list of signals. The ones you wire on day one
are:

- :ref:`configured <class_MultiplayerTree_signal_configured>`: the API and
  scene manager are ready. Use this to register custom services or to read
  :ref:`role <class_MultiplayerTree_property_role>` for the first time.
- :ref:`player_joined <class_MultiplayerTree_signal_player_joined>`:
  fires on **every** peer when the server accepts a new player. Receives a
  :ref:`ResolvedJoin <class_ResolvedJoin>` with the peer id, username, and
  resolved spawner path.
- :ref:`local_player_joined <class_MultiplayerTree_signal_local_player_joined>`
  the same event, but only fires when the joined peer is *this* one.
  Useful for camera setup and HUD bootstrapping.
- :ref:`peer_disconnected <class_MultiplayerTree_signal_peer_disconnected>`
  the underlying transport dropped a peer. Already de-duplicated. The
  tree forgets the peer's roster entry before emitting.
- :ref:`server_disconnected <class_MultiplayerTree_signal_server_disconnected>`
  fired on the client when the server goes away. The state machine has
  already moved back toward :ref:`OFFLINE <class_MultiplayerTree_constant_OFFLINE>` by the time the handler runs, so it
  is safe to immediately call :ref:`disconnect_player() <class_MultiplayerTree_method_disconnect_player>` or change scenes.

Tearing down
------------

Closing a session is just calling
:ref:`disconnect_player() <class_MultiplayerTree_method_disconnect_player>`.
It flushes the local peer's :ref:`SaveComponent <class_SaveComponent>` data,
closes the multiplayer peer, awaits the server's confirmation (with a 3
second cap), and then resets :ref:`state <class_MultiplayerTree_property_state>` to :ref:`OFFLINE <class_MultiplayerTree_constant_OFFLINE>` and :ref:`role <class_MultiplayerTree_property_role>` to
:ref:`NONE <class_MultiplayerTree_constant_NONE>`. If the session ran with an automatically-spawned sibling
``Server`` tree, that node is queue-freed as part of the teardown.

The tree also cleans itself up when removed from the tree mid-session.
:godot:`_exit_tree() <Node#class_node_private_method__exit_tree>` unmounts the API, closes the peer, and clears the auth
coordinator and service registry so nothing keeps a strong reference back
to the dying tree.

.. tip::

    Networked never assumes you want to reconnect on the same tree
    instance. Tear it down, free it, and add a fresh one. The addon is
    cheap to instantiate. The minimal
    ``addons/networked/connect/connect_overlay.tscn`` example follows
    that pattern and is a good model to copy.
