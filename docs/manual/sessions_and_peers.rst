.. _doc_manual_sessions_and_peers:

Sessions and peers
==================

Once the :ref:`MultiplayerTree <class_MultiplayerTree>` is up, the rest of
the addon is built around two ideas that ride on top of Godot's existing
peer-id model: the *session roster* and the *per-peer context*. This page
explains both, then walks through the join handshake so you can intercept
it (for authentication, name collisions, or richer player metadata) with
confidence.

The session roster
------------------

Godot's :godot:`SceneMultiplayer <SceneMultiplayer>` knows about peers as
opaque integer ids. That is enough for raw RPCs, but not enough for
gameplay: you usually want to know a peer's username, which scene they have
been routed into, and the data they presented when they joined.

The :ref:`MultiplayerTree <class_MultiplayerTree>` keeps a roster of every
:ref:`ResolvedJoin <class_ResolvedJoin>` it has accepted. The roster is
authoritative on the server, mirrored on every client, and re-synchronized
to late joiners when they connect. You read it via two paired methods:

- :ref:`get_joined_players() <class_MultiplayerTree_method_get_joined_players>`
  returns every accepted player as an array of
  :ref:`ResolvedJoin <class_ResolvedJoin>`.
- :ref:`get_joined_player() <class_MultiplayerTree_method_get_joined_player>`
  resolves a single peer id, or returns ``null`` if the peer never joined or
  has been forgotten on disconnect.

Roster entries are emitted on the
:ref:`player_joined <class_MultiplayerTree_signal_player_joined>` signal as
they arrive, including the late-join "catch up" packet, so a UI subscribed
to that signal does not need to scan the roster on start-up.

The per-peer context
--------------------

Server-side systems frequently need to attach scratch data to a peer:
"what is this peer's current save container?", "did this peer pass the
post-spawn handshake?", "what scene have we routed them into?". Doing that
with static dictionaries is fragile. They leak across reconnections and
across tests, and they couple every component to the same global namespace.

Networked solves this with :ref:`NetwPeerContext <class_NetwPeerContext>`
buckets. Each component declares an inner Bucket class extending
:godot:`RefCounted <RefCounted>`, then asks the peer's context for a typed
instance:

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name MyComponent
    extends Node

    class Bucket extends RefCounted:
        var ready_to_play: bool = false
        var custom_score: int = 0

    func mark_ready(peer_id: int) -> void:
        var ctx := MultiplayerTree.for_node(self).get_peer_context(peer_id)
        var bucket := ctx.get_bucket(Bucket) as Bucket
        bucket.ready_to_play = true

The roster owns one :ref:`NetwPeerContext <class_NetwPeerContext>` per peer
and lazily creates buckets the first time you ask for them. Because the key
is the bucket's *class object*, two unrelated components can hold per-peer
state without ever importing each other.

When a peer disconnects, the tree drops its context. Bucket instances are
:godot:`RefCounted <RefCounted>`, so any lingering reference held by an RPC
handler stays alive long enough to finish processing, but nothing leaks
into the next session.

The join handshake
------------------

A client does not become a "joined player" the instant its transport peer
connects. It first sends a :ref:`JoinPayload <class_JoinPayload>` to the
server, which validates it, runs the auth pipeline, resolves field defaults,
and only then broadcasts an acceptance to every peer. The handshake looks
like this:

1. :ref:`join() <class_MultiplayerTree_method_join>` or
   :ref:`join_or_host() <class_MultiplayerTree_method_join_or_host>`:
   the transport peer becomes connected.
2. :ref:`submit_join() <class_MultiplayerTree_method_submit_join>`: the payload is serialized and sent to
   the server via the
   :ref:`request_join_player <class_MultiplayerTree_method_request_join_player>`
   RPC.
3. The server unpacks the payload, runs
   :ref:`AuthCoordinator.resolve_identity() <class_AuthCoordinator>`, and
   builds a :ref:`ResolvedJoin <class_ResolvedJoin>`.
4. Username collisions are resolved (or the offender is kicked).
5. The server remembers the resolved join and broadcasts it to every peer. The fresh peer also receives the existing roster.
6. Every peer's
   :ref:`player_joined <class_MultiplayerTree_signal_player_joined>` signal
   fires. The accepted peer's
   :ref:`local_player_joined <class_MultiplayerTree_signal_local_player_joined>`
   fires in addition.

You will rarely call :ref:`submit join <class_MultiplayerTree_method_submit_join>`
yourself. The
:ref:`join() <class_MultiplayerTree_method_join>` and
:ref:`join_or_host() <class_MultiplayerTree_method_join_or_host>` flows
do it for you. But you can intercept any step:

- Provide an :ref:`auth_provider <class_NetwAuthProvider>` to validate
  credentials before the join is accepted.
- Subclass :ref:`SessionRoster <class_SessionRoster>` to customise username
  collisions, kick policies, or roster persistence.
- Listen to
  :ref:`player_joined <class_MultiplayerTree_signal_player_joined>` and
  refuse to spawn certain payloads. The spawn flow is decoupled, so a
  joined player who is never spawned simply waits in the lobby.

.. note::

    The handshake is intentionally idempotent on remote peers. Receiving
    the same :ref:`ResolvedJoin <class_ResolvedJoin>` twice (from the
    broadcast and from the catch-up packet) is a no-op:
    :ref:`SessionRoster <class_SessionRoster>` keys by peer id and the
    second remembrance returns false without re-emitting the signal.

A worked example
----------------

The ``examples/bomber`` project ships a small gamestate node that
demonstrates the pattern end-to-end. Its ``BomberGamestate`` registers
itself as a service, then connects to
:ref:`player_joined <class_MultiplayerTree_signal_player_joined>` and
:ref:`peer_disconnected <class_MultiplayerTree_signal_peer_disconnected>`
to keep a ``players`` dictionary in sync with the roster:

.. tabs::
 .. code-tab:: gdscript GDScript

    func setup_connections() -> void:
        ctx.tree.player_joined.connect(_on_player_joined)
        ctx.tree.peer_disconnected.connect(_on_peer_disconnected)
        ctx.tree.connected_to_server.connect(_on_connected_ok)
        ctx.tree.server_disconnected.connect(_on_server_disconnected)

    func _on_player_joined(rj: ResolvedJoin) -> void:
        players[rj.peer_id] = rj.username
        player_list_changed.emit()

That is the entire bridge between the multiplayer roster and the lobby UI.
Note how the gamestate never touches RPCs itself. It reads the resolved
data the tree has already validated and broadcast.
.
