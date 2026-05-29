.. _doc_manual_transport_backends:

Transport backends
==================

The :ref:`BackendPeer <class_BackendPeer>` resource connects Networked's
session lifecycle to the underlying transport. A backend's main job is to
produce a configured
:godot:`MultiplayerPeer <MultiplayerPeer>` when the tree asks for one --
everything else (session state, roster, scene replication) is the same
regardless of whether your bytes are travelling over UDP, WebSocket, or a
Steam relay.

This page covers the four built-in backends, when to pick each one, and how
to write a new one for transports the addon does not ship.

Choosing a backend
------------------

If you do not have a strong reason to pick otherwise, start with
:ref:`ENetBackend <class_ENetBackend>` for desktop projects and
:ref:`WebSocketBackend <class_WebSocketBackend>` for projects that need a
web export. Both implement the full backend contract, both support local
loopback, and both will get you through the entire
:ref:`quick start <doc_quick_start>` without changes.

.. list-table::
   :header-rows: 1
   :widths: 25 15 15 45

   * - Backend
     - Embedded server
     - Web export
     - Notes
   * - :ref:`ENetBackend <class_ENetBackend>`
     - Yes
     - No
     - UDP with optional reliability, the default for native LAN play.
   * - :ref:`WebSocketBackend <class_WebSocketBackend>`
     - Yes
     - Yes
     - TCP-based, works in HTML5, slightly higher latency than ENet.
   * - WebRTC (via `tube <https://github.com/koopmyers/tube>`__)
     - No (peer-to-peer)
     - Yes
     - Requires a signalling server. Useful for matchmade lobbies without
       running your own authoritative server.
   * - Steam lobby
     - No (Steam-managed)
     - No
     - Adopts a peer produced by Steam's lobby system. The tree uses
       :ref:`adopt_peer() <class_MultiplayerTree_method_adopt_peer>` rather
       than going through
       :ref:`host player <class_MultiplayerTree_method_host_player>` or
       :ref:`direct join <class_MultiplayerTree_method_join_direct>`.

The "embedded server" column matters when you call
:ref:`auto-connect <class_MultiplayerTree_method_auto_connect_player>` with
a local address. Backends that report
:ref:`supports_embedded_server() <class_BackendPeer_method_supports_embedded_server>` participate
in the host-on-demand flow described in
:ref:`doc_manual_multiplayer_tree`. The others fall back to "just host a
lobby and let peers in" semantics.

Configuring a backend
---------------------

Backends are :godot:`Resource <Resource>` subclasses, so they live in the
inspector and serialize cleanly into ``.tscn`` and ``.tres`` files. Drag a
fresh resource onto the
:ref:`backend <class_MultiplayerTree_property_backend>` property of your
tree and tweak its fields (port, max clients, signalling URL) inline. The
tree duplicates the resource at runtime so the same backend asset can be
shared across multiple trees without bleed-through.

.. tip::

    When debugging mismatched configuration, set ``Netw.dbg.level`` to
    :ref:`DEBUG <class_NetwLog_constant_DEBUG>` and look for the :godot:`ENetMultiplayerPeer.create_server() <ENetMultiplayerPeer#class_enetmultiplayerpeer_method_create_server>` / :godot:`WebSocketMultiplayerPeer.create_server() <WebSocketMultiplayerPeer#class_websocketmultiplayerpeer_method_create_server>`
    log lines. Every built-in backend logs the address it tried to bind so
    "port already in use" or "wrong protocol" mistakes surface immediately.

Writing a custom backend
------------------------

A backend is "anything that can hand the tree a
:godot:`MultiplayerPeer <MultiplayerPeer>`". The interface is small enough
to fit on this page:

.. tabs::
 .. code-tab:: gdscript GDScript

     class_name MyBackend
     extends BackendPeer

     func setup(_tree: MultiplayerTree) -> Error:
         # Bring up sockets, fetch ICE candidates, log in to your service.
         return OK

     func create_host_peer(
         _tree: MultiplayerTree
     ) -> MultiplayerPeer:
         var peer := SomeMultiplayerPeer.new()
         var err := peer.create_server(...)
         if err != OK:
             return null
         return peer

     func create_join_peer(
         _tree: MultiplayerTree, server_address: String, _username: String = ""
     ) -> MultiplayerPeer:
         var peer := SomeMultiplayerPeer.new()
         var err := peer.create_client(server_address, ...)
         if err != OK:
             return null
         return peer

     func poll(_dt: float) -> void:
         # Optional: only needed if your transport requires explicit polling
         # outside the SceneMultiplayer poll() that the tree already calls.
         pass

You only need to override the methods that differ from the base class.
Returning ``null`` from one of the ``create_*_peer`` methods is the
canonical way to signal failure to the tree. Pair it with an error log so
the failure is visible.

Some transports do not fit the request/response shape. Steam lobbies and
WebRTC matchmaking, for example, produce a peer asynchronously from an
external pipeline. For those, return ``null`` from :ref:`create_host_peer() <class_BackendPeer_method_create_host_peer>` and
instead drive the new peer directly onto the tree's
:ref:`api <class_MultiplayerTree_property_api>` (the "adopted API" pattern
used by the `tube <https://github.com/koopmyers/tube>`__ backend). The tree detects that its API has been
swapped and treats the missing return value as success, not failure.

Lifecycle hooks
~~~~~~~~~~~~~~~

The tree calls a handful of lifecycle methods on backends. Override the
ones you need:

- :ref:`setup() <class_BackendPeer_method_setup>`: called once per session before any peer is created.
  Use this for asynchronous bring-up (login, service discovery).
- :ref:`peer_reset_state() <class_BackendPeer_method_peer_reset_state>`: called whenever a session ends. Clear any
  cached lobby/login state your backend holds.
- :ref:`supports_embedded_server() <class_BackendPeer_method_supports_embedded_server>`: return ``true`` if this backend can be
  the target of
  :ref:`auto_connect_player() <class_MultiplayerTree_method_auto_connect_player>`'s
  host-on-demand fallback. Most transports return ``true``. Managed-lobby
  backends (Steam) return ``false``.
- :ref:`query_server_info() <class_BackendPeer_method_query_server_info>`:
  the default implementation runs the ``NPRB`` auth handshake (see
  :doc:`pre_game_connection`) and works for any backend whose peer can drive
  :godot:`SceneMultiplayer <SceneMultiplayer>` auth. Override to return
  :ref:`ServerInfoResult.unsupported() <class_ServerInfoResult_method_unsupported>`
  on transports that cannot (session-id-based: Steam, in-process Local,
  Tube).
- :ref:`get_backend_warnings()
  <class_BackendPeer_method_get_backend_warnings>`: editor-time validation.
  Strings returned here are surfaced as configuration warnings on the tree
  node so misconfigured fields are caught before play.
