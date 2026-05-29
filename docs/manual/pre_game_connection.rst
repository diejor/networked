.. _doc_manual_pre_game_connection:

Pre-game connection
===================

This page covers the gap between "I have a configured
:ref:`BackendPeer <class_BackendPeer>`" and "I am in a session". It
explains the four entry methods on
:ref:`MultiplayerTree <class_MultiplayerTree>`, the :ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
probe used to discover live local servers, the auth protocol that carries
both probes and normal hellos, and the lifecycle limits that protect a
host from probe storms.

Entry methods
-------------

Picking an entry method is a question of intent. All four take a
:ref:`JoinPayload <class_JoinPayload>` describing the player. Transport
identity (backend, address) is supplied as separate arguments, so the
payload carries no URL or transport state.

:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
    Query the address; if a live local server answers, join it as a client,
    otherwise host. The zero-config path for local development and
    listen-server games.

:ref:`join_direct() <class_MultiplayerTree_method_join_direct>`
    Open the backend against a known address as a client. Use when the
    caller already knows there is a server, such as when a server browser row was
    clicked, or an invite was accepted.

:ref:`host_player() <class_MultiplayerTree_method_host_player>`
    Start this tree as the host. Use when the caller already knows it is
    hosting, for example, when a "Host Game" button was clicked.

:ref:`adopt_peer() <class_MultiplayerTree_method_adopt_peer>`
    Attach a pre-connected :godot:`MultiplayerPeer <MultiplayerPeer>`
    produced by an external system (Steam lobby, matchmaker). Skips the
    backend setup, plugs the peer into the tree's api, and finalizes role
    based on the peer's unique id.

.. code-block:: gdscript

    var join := JoinPayload.new()
    join.username = "alice"

    # Auto-detect: join if someone is hosting locally, else host.
    await tree.auto_connect_player(tree.backend, "localhost", join)

    # Explicit join to a known remote server.
    await tree.join_direct(tree.backend, "203.0.113.42", join)

    # Explicit host.
    await tree.host_player(join)

    # Adopt a peer produced by a lobby provider.
    await tree.adopt_peer(steam_peer, join)

Discovering live servers
------------------------

A pre-game UI, such as a server browser, a "join localhost or host?" prompt,
or a recent-servers list, needs to know whether an address has a live
server *without* opening a full session. That is what
:ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
does: the backend opens a transient peer, sends one auth-phase packet,
decodes the reply, and tears down.

.. code-block:: gdscript

    var result: ServerInfoResult = await backend.query_server_info(
        "203.0.113.42", 2.0
    )
    if result.is_ok():
        print("%d players, %d ms ping" % [
            result.info.players, result.latency_ms,
        ])
    else:
        print("not reachable: ", result.status)

The reply is a :ref:`ServerInfoResult <class_ServerInfoResult>` whose
:ref:`status <class_ServerInfoResult_property_status>` is one of :ref:`OK <class_ServerInfoResult_constant_OK>`,
:ref:`UNREACHABLE <class_ServerInfoResult_constant_UNREACHABLE>`, :ref:`TIMEOUT <class_ServerInfoResult_constant_TIMEOUT>`, :ref:`UNSUPPORTED <class_ServerInfoResult_constant_UNSUPPORTED>`, :ref:`BUSY <class_ServerInfoResult_constant_BUSY>`, or :ref:`ERROR <class_ServerInfoResult_constant_ERROR>`. On
:ref:`OK <class_ServerInfoResult_constant_OK>`, :ref:`info <class_ServerInfoResult_property_info>` is a populated
:ref:`ServerInfo <class_ServerInfo>` (player count, motd, game mode, a
metadata bag for custom fields).

Hosts customize what gets reported by assigning a
:ref:`ServerInfoSource <class_ServerInfoSource>` to
:ref:`server_info_source <class_MultiplayerTree_property_server_info_source>`
on the tree. The default
(:ref:`DefaultServerInfoSource <class_DefaultServerInfoSource>`) reports a
live player count and marks :ref:`ServerInfo.is_local_listener <class_ServerInfo_property_is_local_listener>` as ``true`` so callers can
tell a live local host from a closed port. Override for richer metadata:

.. code-block:: gdscript

    class_name BomberServerInfoSource extends ServerInfoSource

    func build_server_info(tree: MultiplayerTree) -> ServerInfo:
        var info := ServerInfo.new()
        info.is_local_listener = true
        info.players = tree.get_joined_players().size()
        info.max_players = 8
        info.game_mode = &"capture-the-flag"
        info.motd = "Friday night session"
        return info

Backends that cannot run a SceneMultiplayer auth handshake (session-id
transports: Steam, in-process Local, Tube) override
:ref:`query_server_info() <class_BackendPeer_method_query_server_info>` to return
:ref:`ServerInfoResult.unsupported() <class_ServerInfoResult_method_unsupported>`.
:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
treats :ref:`UNSUPPORTED <class_ServerInfoResult_constant_UNSUPPORTED>` as "no listener available" and falls through to
hosting.

The auth protocol
-----------------

Both probes and normal client hellos ride the same
:godot:`SceneMultiplayer <SceneMultiplayer>` auth phase, distinguished by
a 4-byte magic prefix on the first packet:

- :ref:`NHEL <class_AuthProtocol_property_MAGIC_HELLO>` - *Networked Hello*. A normal client opening a session. The
  configured :ref:`NetwAuthProvider <class_NetwAuthProvider>`'s payload (if
  any) is wrapped inside.
- :ref:`NPRB <class_AuthProtocol_property_MAGIC_PROBE>` - *Networked Probe*. A transient browser/probe peer requesting
  server metadata.

The server's auth callback is **always installed**, whether or not an
auth provider is configured: the callback dispatches by magic and decides
what to do. Unknown payloads disconnect fail-closed.

The isolation guarantee that follows from this design is the load-bearing
one: :godot:`SceneMultiplayer <SceneMultiplayer>` only admits a peer to connected peers (the
set returned by :godot:`get_peers() <MultiplayerAPI>`) when :godot:`complete_auth() <SceneMultiplayer>` is called for it.
Probe replies never call :godot:`complete_auth() <SceneMultiplayer>`. **Probes never enter
gameplay state** - not :ref:`MultiplayerTree.peer_connected <class_MultiplayerTree_signal_peer_connected>`,
not the session roster, not interest computation, not RPC dispatch. A
heavy probe load on a host costs auth slots, nothing else.

Probe lifecycle and limits
--------------------------

The probe lifecycle is **client-owned**:

1. Client opens a transient peer and connects.
2. Client sends :ref:`NPRB <class_AuthProtocol_property_MAGIC_PROBE>` in the :godot:`peer_authenticating <SceneMultiplayer>` callback.
3. Server's :godot:`auth_callback <SceneMultiplayer>` decodes the magic, builds a
   :ref:`ServerInfo <class_ServerInfo>` from the configured
   :ref:`ServerInfoSource <class_ServerInfoSource>`, and sends the reply.
4. Client decodes the reply, returns the
   :ref:`ServerInfoResult <class_ServerInfoResult>`, and closes its peer.
5. Server sees the peer disconnect (or :godot:`auth_timeout <SceneMultiplayer>` reaps it) and
   releases the pending slot.

The server **never calls** :godot:`disconnect_peer() <MultiplayerAPI>` for a probe peer. The
ENet send-then-disconnect race that would otherwise drop replies is
avoided entirely by handing termination to the side that initiated the
probe.

Two limits protect the host from misbehaving probers:

- PROBE_RATE_LIMIT (10/sec by default on :ref:`AuthCoordinator <class_AuthCoordinator>`): a rolling cap on probe
  replies per second. Excess probes get :ref:`BUSY <class_ServerInfoResult_constant_BUSY>` until the window
  reopens.
- MAX_ACTIVE_PROBES (32 by default on :ref:`AuthCoordinator <class_AuthCoordinator>`): a cap on concurrent pending
  probes tracked by the coordinator. Excess probes also get :ref:`BUSY <class_ServerInfoResult_constant_BUSY>`.

Stragglers (clients that crash before closing, or that never close on
purpose) are cleaned up by
:godot:`SceneMultiplayer.auth_timeout <SceneMultiplayer>` (default 3s), which reaps any
pending peers past their deadline. Setting :godot:`SceneMultiplayer.auth_timeout <SceneMultiplayer>` to ``0``
disables this cleanup. Do not do that on hosts that accept probes.

.. note::

    These limits bound the host's own data structures. They do not
    substitute for transport-level protection. Hosts exposed to hostile
    internet traffic should sit behind a CDN, a firewall, or ENet's
    bandwidth caps the same as any other UDP server.

Server browser recipe
---------------------

The ``addons/networked/connect/`` subtree ships the primitives needed
to build a Minecraft-style server browser on top of
``query_server_info``:

- :ref:`JoinTarget <class_JoinTarget>` - one row in the list. Either
  a direct target (:ref:`JoinTarget.backend <class_JoinTarget_property_backend>` + :ref:`JoinTarget.address <class_JoinTarget_property_address>`) or an external one
  (:ref:`JoinTarget.provider_id <class_JoinTarget_property_provider_id>` + :ref:`JoinTarget.remote_id <class_JoinTarget_property_remote_id>` resolved through a
  :ref:`ProviderRegistry <class_ProviderRegistry>`). The :ref:`JoinTarget.backend <class_JoinTarget_property_backend>` field is a template, and
  :ref:`JoinTarget.make_backend_instance() <class_JoinTarget_method_make_backend_instance>` returns a fresh duplicate so probe and
  join paths do not share runtime state.
- :ref:`ServerList <class_ServerList>` - a typed array of targets
  persisted to ``user://servers.tres``. :ref:`ServerList.load_or_new() <class_ServerList_method_load_or_new>` is empty on
  first run, and :ref:`ServerList.save() <class_ServerList_method_save>` writes the current list back.
- :ref:`ProbeSession <class_ProbeSession>` - thin wrapper that calls
  :ref:`query_server_info() <class_BackendPeer_method_query_server_info>` on a fresh backend instance and emits the
  result.
- :ref:`ProbeManager <class_ProbeManager>` - a :godot:`Node <Node>` that caps
  concurrent sessions (:ref:`ProbeManager.max_concurrent <class_ProbeManager_property_max_concurrent>`, default 6) below the
  server-side MAX_ACTIVE_PROBES cap on :ref:`AuthCoordinator <class_AuthCoordinator>`. :ref:`ProbeManager.cancel_all() <class_ProbeManager_method_cancel_all>` suppresses
  pending callbacks but does not abort the inner
  :ref:`query_server_info() <class_BackendPeer_method_query_server_info>` - transient peers tear themselves down on
  their own timeout/completion path.
- :ref:`ProviderRegistry <class_ProviderRegistry>` - a :godot:`Node <Node>`
  mapping :godot:`StringName <StringName>` ids to :ref:`LobbyProvider <class_LobbyProvider>` instances. The
  browser looks providers up at join time by :ref:`JoinTarget.provider_id <class_JoinTarget_property_provider_id>`.

The reference scene at
``addons/networked/connect/server_browser.tscn`` wires these together:
it loads the persisted list, fires one probe per direct target through
a :ref:`ProbeManager <class_ProbeManager>`, and renders rows grouped by source. Direct rows
dispatch through :ref:`MultiplayerTree.auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`. Provider
rows look up the provider in the registry, call :ref:`LobbyProvider.join_lobby() <class_LobbyProvider_method_join_lobby>` with
:ref:`JoinTarget.remote_id <class_JoinTarget_property_remote_id>`, await :ref:`LobbyProvider.peer_ready <class_LobbyProvider_signal_peer_ready>`, and dispatch through
:ref:`MultiplayerTree.adopt_peer() <class_MultiplayerTree_method_adopt_peer>`.

Wiring it up looks like:

.. code-block:: gdscript

    var browser := preload(
        "res://addons/networked/connect/server_browser.tscn"
    ).instantiate()
    browser.tree_path = tree.get_path()
    browser.backend_templates = [ENetBackend.new(), WebSocketBackend.new()]
    add_child(browser)

    # Optional: surface lobbies from a SteamLobbyProvider in the same list.
    browser.register_provider(&"steam", steam_provider)

A minimal address-only form lives at
``addons/networked/connect/connect_overlay.tscn`` for projects that do
not need the full browser. It emits a single ``connect_requested``
signal carrying a direct :ref:`JoinTarget <class_JoinTarget>`.
