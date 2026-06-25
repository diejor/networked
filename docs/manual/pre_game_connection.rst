.. _doc_manual_pre_game_connection:

Pre-game connection
===================

This page covers the gap between "I have a configured
:ref:`BackendPeer <class_BackendPeer>`" and "I am in a session". It
explains the four entry methods on
:ref:`MultiplayerTree <class_MultiplayerTree>`, the :ref:`probe_server_info() <class_BackendPeer_method_probe_server_info>`
probe used to discover live servers on cheap direct transports, the auth
protocol that carries both probes and normal hellos, and the lifecycle
limits that protect a host from probe storms.

Entry methods
-------------

Picking an entry method is a question of intent. All three take a
:ref:`JoinPayload <class_JoinPayload>` describing the player. Transport
identity (backend, address) is supplied via a :ref:`JoinTarget <class_JoinTarget>`
passed to the method, so the payload itself carries no transport state.

:ref:`join_or_host() <class_MultiplayerTree_method_join_or_host>`
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

    # Probe first. Join if someone is hosting locally, else host.
    await tree.join_or_host(target, join)

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
:ref:`probe_server_info() <class_BackendPeer_method_probe_server_info>`
does on cheap direct transports (ENet, WebSocket): the backend opens a
transient peer via :ref:`AuthProtocol.Client <class_AuthProtocol_Client>`, sends one
auth-phase packet on the same port a real join would use, decodes the reply,
and tears down. It is *not* a universal probe - brokered transports (Steam,
WebRTC trackers) override :ref:`probe_server_info() <class_BackendPeer_method_probe_server_info>`
with their own discovery or return
:ref:`BackendPeer.ProbeResult.unsupported() <class_BackendPeer_ProbeResult_method_unsupported>`.

.. code-block:: gdscript

    var result: BackendPeer.ProbeResult = await backend.probe_server_info(
        "203.0.113.42", 2.0
    )
    if result.is_ok():
        print("%d players, %d ms ping" % [
            result.info.players, result.latency_ms,
        ])
    else:
        print("not reachable: ", result.status)

The reply is a :ref:`BackendPeer.ProbeResult <class_BackendPeer_ProbeResult>` whose
:ref:`status <class_BackendPeer_ProbeResult_property_status>` is one of :ref:`OK <class_BackendPeer_ProbeResult_constant_OK>`,
:ref:`UNREACHABLE <class_BackendPeer_ProbeResult_constant_UNREACHABLE>`, :ref:`TIMEOUT <class_BackendPeer_ProbeResult_constant_TIMEOUT>`, :ref:`UNSUPPORTED <class_BackendPeer_ProbeResult_constant_UNSUPPORTED>`, :ref:`BUSY <class_BackendPeer_ProbeResult_constant_BUSY>`, or :ref:`ERROR <class_BackendPeer_ProbeResult_constant_ERROR>`. On
:ref:`OK <class_BackendPeer_ProbeResult_constant_OK>`, :ref:`info <class_BackendPeer_ProbeResult_property_info>` is a populated
:ref:`ServerDescriptor.Info <class_ServerDescriptor_ServerInfo>` (player count, motd, game mode, a
metadata bag for custom fields).

Hosts customize what gets reported by assigning a
:ref:`ServerDescriptor <class_ServerDescriptor>` to
:ref:`server_info_source <class_MultiplayerTree_property_server_info_source>`
on the tree. The default
(:ref:`DefaultServerDescriptor <class_DefaultServerDescriptor>`) reports a
live player count and marks :ref:`ServerDescriptor.Info.is_local_listener <class_ServerDescriptor_ServerInfo_property_is_local_listener>` as ``true`` so callers can
tell a live local host from a closed port. Override for richer metadata:

.. code-block:: gdscript

    class_name BomberServerInfoSource extends ServerDescriptor

    func build_server_info(tree: MultiplayerTree) -> ServerDescriptor.Info:
        var info := ServerDescriptor.Info.new()
        info.is_local_listener = true
        info.players = tree.get_joined_players().size()
        info.max_players = 8
        info.game_mode = &"capture-the-flag"
        info.motd = "Friday night session"
        return info

The same-port probe is opt-in: only cheap direct transports (ENet,
WebSocket) enable it by delegating to
:ref:`AuthProtocol.Client <class_AuthProtocol_Client>`. The
:ref:`BackendPeer <class_BackendPeer>` default returns
:ref:`BackendPeer.ProbeResult.unsupported() <class_BackendPeer_ProbeResult_method_unsupported>`,
so session-id/lobby transports (Steam, in-process Local) and WebRTC (whose
auth handshake requires a full, expensive ICE round trip) stay unsupported
unless they implement their own discovery.
:ref:`join_or_host() <class_MultiplayerTree_method_join_or_host>`
treats any non-:ref:`OK <class_BackendPeer_ProbeResult_constant_OK>` result
(including :ref:`UNSUPPORTED <class_BackendPeer_ProbeResult_constant_UNSUPPORTED>`) as
"no listener available" and falls through to hosting.

Platform availability
---------------------

Discovery answers three independent questions, and it helps to keep them
apart:

- **Availability** - can this transport run on this platform and build at
  all? ENet and Steam have no web export; WebSocket and WebRTC do.
- **Discovery mechanism** - is status learned by poking an address (a probe)
  or handed over by a directory (a Steam lobby list)?
- **Reachability** - is *this specific server* up right now?

Only reachability lives on
:ref:`BackendPeer.ProbeResult <class_BackendPeer_ProbeResult>`. Availability is a separate
axis answered by
:ref:`is_available() <class_BackendPeer_method_is_available>`, queried directly
and never routed through a result status. This matters because
:ref:`UNSUPPORTED <class_BackendPeer_ProbeResult_constant_UNSUPPORTED>` already means
"this backend skips probing" (Steam, Local, WebRTC) - a backend that is
unsupported for probing still connects fine. A backend that is *unavailable*
cannot connect at all, which is a different thing.

Self-contained transports answer availability with a platform feature check:

.. code-block:: gdscript

    func is_available() -> bool:
        return not OS.has_feature("web")

Directory-mediated transports (Steam) leave the runtime answer to the
directory, which reports
:ref:`provider_unavailable <class_LobbyDirectory_signal_provider_unavailable>`
when its transport is missing.
:ref:`ConnectBrowser <class_ConnectBrowser>` uses availability to filter the
host and join flows: unavailable transports are dropped from the Host / Add /
Join pickers, saved targets that cannot run here are shown as ``Unavailable``
and never probed, and a join against one is refused before it starts.

The auth protocol
-----------------

Both probes and normal client hellos ride the same
:godot:`SceneMultiplayer <SceneMultiplayer>` auth phase, distinguished by
a 4-byte magic prefix on the first packet:

- :ref:`NHEL <class_AuthProtocol_property_MAGIC_HELLO>` - *Networked Hello*. A normal client opening a session. The
  configured :ref:`NetwAuth <class_NetwAuth>`'s payload (if
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
   dispatches ``NPRB`` to :ref:`AuthProtocol.Responder <class_AuthProtocol_Responder>`,
   which builds a :ref:`ServerDescriptor.Info <class_ServerDescriptor_ServerInfo>` from the configured
   :ref:`ServerDescriptor <class_ServerDescriptor>` and sends the reply.
4. Client decodes the reply, returns the
   :ref:`BackendPeer.ProbeResult <class_BackendPeer_ProbeResult>`, and closes its peer.
5. Server sees the peer disconnect (or :godot:`auth_timeout <SceneMultiplayer>` reaps it) and
   releases the pending slot.

The server **never calls** :godot:`disconnect_peer() <MultiplayerAPI>` for a probe peer. The
ENet send-then-disconnect race that would otherwise drop replies is
avoided entirely by handing termination to the side that initiated the
probe.

Two limits protect the host from misbehaving probers:

- :ref:`PROBE_RATE_LIMIT <class_AuthProtocol_Responder_constant_PROBE_RATE_LIMIT>` (10/sec by default on :ref:`AuthProtocol.Responder <class_AuthProtocol_Responder>`): a rolling cap on probe
  replies per second. Excess probes get :ref:`BUSY <class_BackendPeer_ProbeResult_constant_BUSY>` until the window
  reopens.
- :ref:`MAX_ACTIVE_PROBES <class_AuthProtocol_Responder_constant_MAX_ACTIVE_PROBES>` (32 by default on :ref:`AuthProtocol.Responder <class_AuthProtocol_Responder>`): a cap on concurrent pending
  probes tracked by the responder. Excess probes also get :ref:`BUSY <class_BackendPeer_ProbeResult_constant_BUSY>`.

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
``probe_server_info``:

- :ref:`JoinTarget <class_JoinTarget>` - one row in the list. It bundles a
  :ref:`BackendPeer <class_BackendPeer>` template and an
  :ref:`address <class_JoinTarget_property_address>` string (e.g. host:port
  or Steam lobby ID) along with display labels and metadata. The
  :ref:`JoinTarget.backend <class_JoinTarget_property_backend>` field is a template, and
  :ref:`JoinTarget.make_backend_instance() <class_JoinTarget_method_make_backend_instance>` returns a fresh duplicate so probe and
  join paths do not share runtime state.
- :ref:`ConnectSession <class_ConnectSession>` persists saved targets to
  ``user://servers.tres`` by default. Use
  :ref:`load_server_list() <class_ConnectSession_method_load_server_list>` and
  :ref:`save_server_list() <class_ConnectSession_method_save_server_list>` to
  read or write the current saved target list.
- :ref:`ConnectSession <class_ConnectSession>` runs capped probe work through
  fresh backend instances. Pending callbacks are suppressed when a refresh is
  cancelled, while transient peers tear themselves down on their own timeout
  or completion path.
- :ref:`ConnectSession <class_ConnectSession>` keeps a private directory
  registry mapping :godot:`StringName <StringName>` ids to
  :ref:`LobbyDirectory <class_LobbyDirectory>` instances.

The reference scene at
``addons/networked/connect/ui/connect_browser.tscn`` wires these together:
it loads the persisted list, fires one probe per saved target through
:ref:`ConnectSession <class_ConnectSession>`, and renders rows grouped by provenance.
All rows dispatch uniformly through :ref:`MultiplayerTree.join() <class_MultiplayerTree_method_join>`
regardless of the underlying transport backend.

Wiring it up looks like:

.. code-block:: gdscript

    var browser := preload(
        "res://addons/networked/connect/ui/connect_browser.tscn"
    ).instantiate()
    browser.tree = multiplayer_tree
    browser.backend_templates = [ENetBackend.new(), WebSocketBackend.new()]
    add_child(browser)

    # Optional: surface lobbies from a SteamLobbyDirectory in the same list.
    var session := Netw.ctx(multiplayer_tree).connect
    session.register_directory(&"steam", steam_directory)
