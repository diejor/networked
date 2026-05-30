.. _doc_manual_pre_game_connection:

Pre-game connection
===================

This page covers the gap between "I have a configured
:ref:`BackendPeer <class_BackendPeer>`" and "I am in a session". It
explains the four entry methods on
:ref:`MultiplayerTree <class_MultiplayerTree>`, the :ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
probe used to discover live servers on cheap direct transports, the auth
protocol that carries both probes and normal hellos, and the lifecycle
limits that protect a host from probe storms.

Entry methods
-------------

Picking an entry method is a question of intent. All three take a
:ref:`JoinPayload <class_JoinPayload>` describing the player. Transport
identity (backend, address) is supplied via a :ref:`JoinTarget <class_JoinTarget>`
passed to the method, so the payload itself carries no transport state.

:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
    Query the address; if a live local server answers, join it as a client,
    otherwise host. The zero-config path for local development and
    listen-server games.

:ref:`join() <class_MultiplayerTree_method_join>`
    Open the backend against a known address as a client. Use when the
    caller already knows there is a server, such as when a server browser row was
    clicked, or an invite was accepted.

:ref:`host_player() <class_MultiplayerTree_method_host_player>`
    Start this tree as the host. Use when the caller already knows it is
    hosting, for example, when a "Host Game" button was clicked.

.. code-block:: gdscript

    var join := JoinPayload.new()
    join.username = "alice"

    # Build the join target (specifying backend and address).
    var target := JoinTarget.new()
    target.backend = WebSocketBackend.new()
    target.address = "localhost"

    # Auto-detect: join if someone is hosting locally, else host.
    await tree.auto_connect_player(target, join)

    # Explicit join to a known remote server.
    target.address = "203.0.113.42"
    await tree.join(target, join)

    # Explicit host.
    await tree.host_player(join)

Discovering live servers
------------------------

A pre-game UI, such as a server browser, a "join localhost or host?" prompt,
or a recent-servers list, needs to know whether an address has a live
server *without* opening a full session. That is what
:ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
does on cheap direct transports (ENet, WebSocket): the backend opens a
transient peer via :ref:`AuthProbeClient <class_AuthProbeClient>`, sends one
auth-phase packet on the same port a real join would use, decodes the reply,
and tears down. It is *not* a universal probe - brokered transports (Steam,
WebRTC trackers) override :ref:`query_server_info() <class_BackendPeer_method_query_server_info>`
with their own discovery or return
:ref:`ServerInfoResult.unsupported() <class_ServerInfoResult_method_unsupported>`.

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

The same-port probe is opt-in: only cheap direct transports (ENet,
WebSocket) enable it by delegating to
:ref:`AuthProbeClient <class_AuthProbeClient>`. The
:ref:`BackendPeer <class_BackendPeer>` default returns
:ref:`ServerInfoResult.unsupported() <class_ServerInfoResult_method_unsupported>`,
so session-id transports (Steam, in-process Local, Tube) and WebRTC (whose
auth handshake requires a full, expensive ICE round trip) stay unsupported
unless they implement their own discovery.
:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
treats any non-:ref:`OK <class_ServerInfoResult_constant_OK>` result
(including :ref:`UNSUPPORTED <class_ServerInfoResult_constant_UNSUPPORTED>`) as
"no listener available" and falls through to hosting.

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
3. Server's :godot:`auth_callback <SceneMultiplayer>` decodes the magic and
   dispatches ``NPRB`` to :ref:`AuthProbeResponder <class_AuthProbeResponder>`,
   which builds a :ref:`ServerInfo <class_ServerInfo>` from the configured
   :ref:`ServerInfoSource <class_ServerInfoSource>` and sends the reply.
4. Client decodes the reply, returns the
   :ref:`ServerInfoResult <class_ServerInfoResult>`, and closes its peer.
5. Server sees the peer disconnect (or :godot:`auth_timeout <SceneMultiplayer>` reaps it) and
   releases the pending slot.

The server **never calls** :godot:`disconnect_peer() <MultiplayerAPI>` for a probe peer. The
ENet send-then-disconnect race that would otherwise drop replies is
avoided entirely by handing termination to the side that initiated the
probe.

Two limits protect the host from misbehaving probers:

- PROBE_RATE_LIMIT (10/sec by default on :ref:`AuthProbeResponder <class_AuthProbeResponder>`): a rolling cap on probe
  replies per second. Excess probes get :ref:`BUSY <class_ServerInfoResult_constant_BUSY>` until the window
  reopens.
- MAX_ACTIVE_PROBES (32 by default on :ref:`AuthProbeResponder <class_AuthProbeResponder>`): a cap on concurrent pending
  probes tracked by the responder. Excess probes also get :ref:`BUSY <class_ServerInfoResult_constant_BUSY>`.

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

- :ref:`JoinTarget <class_JoinTarget>` - one row in the list. It bundles a
  :ref:`BackendPeer <class_BackendPeer>` template and an
  :ref:`address <class_JoinTarget_property_address>` string (e.g. host:port
  or Steam lobby ID) along with display labels and metadata. The
  :ref:`JoinTarget.backend <class_JoinTarget_property_backend>` field is a template, and
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
  server-side MAX_ACTIVE_PROBES cap on :ref:`AuthProbeResponder <class_AuthProbeResponder>`. :ref:`ProbeManager.cancel_all() <class_ProbeManager_method_cancel_all>` suppresses
  pending callbacks but does not abort the inner
  :ref:`query_server_info() <class_BackendPeer_method_query_server_info>` - transient peers tear themselves down on
  their own timeout/completion path.
- :ref:`DirectoryRegistry <class_DirectoryRegistry>` - a :godot:`Node <Node>`
  mapping :godot:`StringName <StringName>` ids to :ref:`LobbyDirectory <class_LobbyDirectory>` instances.

The reference scene at
``addons/networked/connect/server_browser.tscn`` wires these together:
it loads the persisted list, fires one probe per saved target through
a :ref:`ProbeManager <class_ProbeManager>`, and renders rows grouped by provenance.
All rows dispatch uniformly through :ref:`MultiplayerTree.join() <class_MultiplayerTree_method_join>`
regardless of the underlying transport backend.

Wiring it up looks like:

.. code-block:: gdscript

    var browser := preload(
        "res://addons/networked/connect/server_browser.tscn"
    ).instantiate()
    browser.tree = multiplayer_tree
    browser.backend_templates = [ENetBackend.new(), WebSocketBackend.new()]
    add_child(browser)

    # Optional: surface lobbies from a SteamLobbyDirectory in the same list.
    var session := Netw.ctx(multiplayer_tree).connect
    session.register_directory(&"steam", steam_directory)

A minimal address-only form lives at
``addons/networked/connect/connect_overlay.tscn`` for projects that do
not need the full browser. It emits a single ``connect_requested``
signal carrying a :ref:`JoinTarget <class_JoinTarget>`.
