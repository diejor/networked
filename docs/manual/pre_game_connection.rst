.. _doc_manual_pre_game_connection:

Pre-game connection
===================

This page covers the gap between *"I have a configured
:ref:`BackendPeer <class_BackendPeer>`"* and *"I am in a session"*. It
explains the four entry methods on
:ref:`MultiplayerTree <class_MultiplayerTree>`, the ``query_server_info``
probe used to discover live local servers, the auth protocol that carries
both probes and normal hellos, and the lifecycle limits that protect a
host from probe storms.

Entry methods
-------------

Picking an entry method is a question of intent. All four take a
:ref:`JoinPayload <class_JoinPayload>` describing the player; transport
identity (backend, address) is supplied as separate arguments, so the
payload carries no URL or transport state.

:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
    Query the address; if a live local server answers, join it as a client,
    otherwise host. The zero-config path for local development and
    listen-server games.

:ref:`join_direct() <class_MultiplayerTree_method_join_direct>`
    Open the backend against a known address as a client. Use when the
    caller already knows there is a server -- a server browser row was
    clicked, an invite was accepted.

:ref:`host_player() <class_MultiplayerTree_method_host_player>`
    Start this tree as the host. Use when the caller already knows it is
    hosting -- a "Host Game" button was clicked.

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

A pre-game UI -- a server browser, a "join localhost or host?" prompt,
a recent-servers list -- needs to know whether an address has a live
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
:ref:`status <class_ServerInfoResult_property_status>` is one of ``OK``,
``UNREACHABLE``, ``TIMEOUT``, ``UNSUPPORTED``, ``BUSY``, or ``ERROR``. On
``OK``, :ref:`info <class_ServerInfoResult_property_info>` is a populated
:ref:`ServerInfo <class_ServerInfo>` (player count, motd, game mode, a
metadata bag for custom fields).

Hosts customize what gets reported by assigning a
:ref:`ServerInfoSource <class_ServerInfoSource>` to
:ref:`server_info_source <class_MultiplayerTree_property_server_info_source>`
on the tree. The default
(:ref:`DefaultServerInfoSource <class_DefaultServerInfoSource>`) reports a
live player count and marks ``is_local_listener = true`` so callers can
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
``query_server_info`` to return
:ref:`ServerInfoResult.unsupported() <class_ServerInfoResult_method_unsupported>`.
:ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`
treats ``UNSUPPORTED`` as "no listener available" and falls through to
hosting.

The auth protocol
-----------------

Both probes and normal client hellos ride the same
:godot:`SceneMultiplayer <SceneMultiplayer>` auth phase, distinguished by
a 4-byte magic prefix on the first packet:

- ``NHEL`` -- *Networked Hello*. A normal client opening a session. The
  configured :ref:`NetwAuthProvider <class_NetwAuthProvider>`'s payload (if
  any) is wrapped inside.
- ``NPRB`` -- *Networked Probe*. A transient browser/probe peer requesting
  server metadata.

The server's auth callback is **always installed**, whether or not an
auth provider is configured: the callback dispatches by magic and decides
what to do. Unknown payloads disconnect fail-closed.

The isolation guarantee that follows from this design is the load-bearing
one: ``SceneMultiplayer`` only admits a peer to ``connected_peers`` (the
set returned by ``get_peers()``) when ``complete_auth`` is called for it.
Probe replies never call ``complete_auth``. **Probes never enter
gameplay state** -- not :ref:`MultiplayerTree.peer_connected <class_MultiplayerTree_signal_peer_connected>`,
not the session roster, not interest computation, not RPC dispatch. A
heavy probe load on a host costs auth slots, nothing else.

Probe lifecycle and limits
--------------------------

The probe lifecycle is **client-owned**:

1. Client opens a transient peer and connects.
2. Client sends ``NPRB`` in the ``peer_authenticating`` callback.
3. Server's ``auth_callback`` decodes the magic, builds a
   :ref:`ServerInfo <class_ServerInfo>` from the configured
   :ref:`ServerInfoSource <class_ServerInfoSource>`, and sends the reply.
4. Client decodes the reply, returns the
   :ref:`ServerInfoResult <class_ServerInfoResult>`, and closes its peer.
5. Server sees the peer disconnect (or ``auth_timeout`` reaps it) and
   releases the pending slot.

The server **never calls** ``disconnect_peer`` for a probe peer. The
ENet send-then-disconnect race that would otherwise drop replies is
avoided entirely by handing termination to the side that initiated the
probe.

Two limits protect the host from misbehaving probers:

- ``PROBE_RATE_LIMIT`` (10/sec by default): a rolling cap on probe
  replies per second. Excess probes get ``BUSY`` until the window
  reopens.
- ``MAX_ACTIVE_PROBES`` (32 by default): a cap on concurrent pending
  probes tracked by the coordinator. Excess probes also get ``BUSY``.

Stragglers (clients that crash before closing, or that never close on
purpose) are cleaned up by
``SceneMultiplayer.auth_timeout`` (default 3s), which reaps any
``pending_peers`` past their deadline. Setting ``auth_timeout = 0``
disables this cleanup -- do not do that on hosts that accept probes.

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

- :ref:`JoinTarget <class_JoinTarget>` -- one row in the list. Either
  a direct target (``backend`` + ``address``) or an external one
  (``provider_id`` + ``remote_id`` resolved through a
  ``ProviderRegistry``). The ``backend`` field is a template;
  ``make_backend_instance()`` returns a fresh duplicate so probe and
  join paths do not share runtime state.
- :ref:`ServerList <class_ServerList>` -- a typed array of targets
  persisted to ``user://servers.tres``. ``load_or_new()`` is empty on
  first run; ``save()`` writes the current list back.
- :ref:`ProbeSession <class_ProbeSession>` -- thin wrapper that calls
  ``query_server_info`` on a fresh backend instance and emits the
  result.
- :ref:`ProbeManager <class_ProbeManager>` -- a ``Node`` that caps
  concurrent sessions (``max_concurrent``, default 6) below the
  server-side ``MAX_ACTIVE_PROBES`` cap. ``cancel_all()`` suppresses
  pending callbacks but does not abort the inner
  ``query_server_info`` -- transient peers tear themselves down on
  their own timeout/completion path.
- :ref:`ProviderRegistry <class_ProviderRegistry>` -- a ``Node``
  mapping ``StringName`` ids to ``LobbyProvider`` instances. The
  browser looks providers up at join time by ``JoinTarget.provider_id``.

The reference scene at
``addons/networked/connect/server_browser.tscn`` wires these together:
it loads the persisted list, fires one probe per direct target through
a ``ProbeManager``, and renders rows grouped by source. Direct rows
dispatch through ``MultiplayerTree.auto_connect_player``; provider
rows look up the provider in the registry, call ``join_lobby`` with
``target.remote_id``, await ``peer_ready``, and dispatch through
``MultiplayerTree.adopt_peer``.

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
signal carrying a direct ``JoinTarget``.
