:allow_comments: False

.. _doc_manual_testing:

Testing
=======

Networked's test helpers let one Godot process run a full multiplayer
session. A test can create a server, connect clients, advance the shared
:godot:`SceneTree <SceneTree>`, and assert on each peer's view of the game
without launching extra editor or export instances.

The helpers live in ``addons/networked_test``. They are normal Godot nodes
and resources, but the common path starts from
:ref:`NetwTestSuite <class_NetwTestSuite>`, the GdUnit4 base class that owns
per-test cleanup, timeout reporting, and log controls.

The mental model
----------------

A multiplayer test needs two separate questions answered:

1. Did the server accept the peer and route the session state correctly?
2. Did the expected node, signal, input, or replicated value appear on the
   peer that should see it?

:ref:`NetwTestHarness <class_NetwTestHarness>` answers the first question
at the addon API level. It builds one dedicated server
:ref:`MultiplayerTree <class_MultiplayerTree>`, adds client
:ref:`MultiplayerTree <class_MultiplayerTree>` nodes, and carries packets
through :ref:`LocalLoopbackBackend <class_LocalLoopbackBackend>`.

:ref:`NetwGameHarness <class_NetwGameHarness>` answers the second question
with a real game scene. It instantiates the game's main scene once per
participant, puts every participant in a slot, and returns a
:ref:`NetwSceneRunner <class_NetwSceneRunner>` for each peer so the test can
drive that peer's input and inspect that peer's world.

:ref:`NetwTestSessionHook <class_NetwTestSessionHook>` resets Networked's
global test state between cases. A live harness should be created in
``before_test()`` or inside the test method, not in ``before()``, because
``before()`` runs before the per-case reset.

Three categories of test
------------------------

Use the smallest harness that still exercises the behavior you care about.

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Category
     - Use it for
     - Typical entry point
   * - Isolated unit tests
     - Pure data structures, resources, and components that do not need a
       session.
     - Plain :ref:`NetwTestSuite <class_NetwTestSuite>` helpers such as
       :ref:`make_test_entity() <class_NetwTestSuite_method_make_test_entity>`.
   * - Session tests
     - Join flows, roster state, scene activation, spawn policy, and
       replication contracts at the addon API boundary.
     - :ref:`make_harness() <class_NetwTestSuite_method_make_harness>` and
       :ref:`NetwTestHarness <class_NetwTestHarness>`.
   * - Game tests
     - Player input, UI-facing game state, scene transitions, and behavior
       that only makes sense in the real main scene.
     - :ref:`make_game_harness() <class_NetwTestSuite_method_make_game_harness>`
       and :ref:`NetwGameHarness <class_NetwGameHarness>`.

Session tests
-------------

Use :ref:`NetwTestHarness <class_NetwTestHarness>` when the test should talk
to Networked's public session API directly. The harness creates the server
tree immediately, but it does not host until the first client connects. That
gives the test a place to register spawnable scenes and configure scene
policies first.

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name TestPlayerSpawn
    extends NetwTestSuite

    const LEVEL := preload(
        "res://addons/networked_test/fixtures/TestLevel.tscn"
    )
    const PLAYER := preload(
        "res://addons/networked_test/fixtures/TestPlayerMinimal.tscn"
    )

    var harness: NetwTestHarness
    var alice: MultiplayerTree

    func before_test() -> void:
        harness = make_harness()
        await harness.setup_factory(NetwTestSuite.create_scene_manager)
        harness.register_spawnable_scene(LEVEL)
        alice = await harness.add_client("alice")

    func test_spawn_assigns_authority_to_owning_peer() -> void:
        var player := harness.spawn_player(alice, PLAYER)
        var alice_player := await harness.wait_for_player(
            alice,
            &"TestLevel"
        )

        assert_that(alice_player).is_not_null()
        assert_that(player.get_multiplayer_authority()).is_equal(
            alice.multiplayer_peer.get_unique_id()
        )

The setup path is the important part:

- :ref:`make_harness() <class_NetwTestSuite_method_make_harness>` creates a
  managed harness.
  :ref:`NetwTestSuite.after_test() <class_NetwTestSuite_method_after_test>`
  tears it down after the case.
- :ref:`setup_factory() <class_NetwTestHarness_method_setup_factory>` gives
  each peer its own fresh
  :ref:`MultiplayerSceneManager <class_MultiplayerSceneManager>`.
- :ref:`register_spawnable_scene() <class_NetwTestHarness_method_register_spawnable_scene>`
  mirrors the level path onto clients created by
  :ref:`add_client() <class_NetwTestHarness_method_add_client>`.
- :ref:`spawn_player() <class_NetwTestHarness_method_spawn_player>` drives the
  server-side spawn.
  :ref:`wait_for_player() <class_NetwTestHarness_method_wait_for_player>`
  waits until the selected client can see it.

When a case needs a second independent session, use
:ref:`make_unmanaged_harness() <class_NetwTestSuite_method_make_unmanaged_harness>`
and call :ref:`teardown() <class_NetwTestHarness_method_teardown>` yourself.
For ordinary cases, prefer the managed harness.

.. tabs::
 .. code-tab:: gdscript GDScript

    func test_two_independent_sessions() -> void:
        var first := make_harness()
        await first.setup_factory(NetwTestSuite.create_scene_manager)

        var second := make_unmanaged_harness()
        await second.setup_factory(NetwTestSuite.create_scene_manager)

        await second.teardown()

Game tests
----------

Use :ref:`NetwGameHarness <class_NetwGameHarness>` when the behavior depends
on the actual game scene. Each participant runs a separate instance of the
main scene inside a :ref:`ParticipantWindow <class_ParticipantWindow>`.
:ref:`add_host() <class_NetwGameHarness_method_add_host>` creates a
listen-server player, and
:ref:`add_client() <class_NetwGameHarness_method_add_client>` connects more
players to it.

The returned :ref:`NetwSceneRunner <class_NetwSceneRunner>` is the handle
for one participant. Use its inherited GdUnit4 input simulation methods to
send input, wait for scenes with
:ref:`await_scene() <class_NetwSceneRunner_method_await_scene>`, and inspect
that participant's copy of a player with
:ref:`await_player() <class_NetwSceneRunner_method_await_player>`.

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name TestQuickStartTwoPlayers
    extends NetwTestSuite

    const MAIN := preload("res://examples/quick_start/Main.tscn")
    const LEVEL_1_SPAWN := (
        "uid://bqi7mvxdnvgch::Player/%MultiplayerEntity"
    )

    var game: NetwGameHarness

    func before_test() -> void:
        game = make_game_harness(MAIN)
        await game.setup()

    func test_bob_sees_alice_move() -> void:
        var spawn := SceneNodePath.new(LEVEL_1_SPAWN)
        var alice := await game.add_host("alice", true, spawn)
        var bob := await game.add_client("bob", true, spawn)

        var alice_on_bob: Node2D = await bob.await_player(&"alice")
        var start := alice_on_bob.position.x

        alice.simulate_action_press("move_right")
        await game.sync_ticks(16)
        alice.simulate_action_release("move_right")
        await game.sync_ticks(16)

        assert_that(alice_on_bob.position.x).is_greater(start)

This test asserts from Bob's view of the world. That is the useful
distinction. Alice's local player might move while replication is broken.
Asking Bob's :ref:`NetwSceneRunner <class_NetwSceneRunner>` for Alice's
player proves that the visible remote copy moved.

Input in a game harness
~~~~~~~~~~~~~~~~~~~~~~~

Every participant shares one process, so the global
:godot:`Input <Input>` singleton cannot represent one player. Slot-routed
tests send events through the participant's
:ref:`NetwSceneRunner <class_NetwSceneRunner>`, and the game should receive
them through
:godot:`_unhandled_input() <Node#class_node_private_method__unhandled_input>`.

.. tip::

   :ref:`InputComponent <class_InputComponent>` and
   :ref:`MoveInputComponent <class_MoveInputComponent>` are built for this
   pattern. The component stores the input state on the player node, while
   the runner scopes the event to the participant slot.

.. tabs::
 .. code-tab:: gdscript GDScript

    func test_client_input_reaches_only_that_client() -> void:
        var host := await game.add_host("host", true, spawn)
        var client := await game.add_client("client", true, spawn)

        var host_player := host.local_player as Node2D
        var client_player := client.local_player as Node2D
        var host_start := host_player.position.x
        var client_start := client_player.position.x

        client.simulate_action_press("move_right")
        await game.sync_ticks(8)
        client.simulate_action_release("move_right")

        assert_that(client_player.position.x).is_greater(client_start)
        assert_float(host_player.position.x).is_equal_approx(host_start, 1.0)

Driving time
------------

Networked tests should wait on named multiplayer events when a helper exists,
and advance ticks when the game simulation itself needs time to run.

:ref:`NetwTestHarness <class_NetwTestHarness>` routes waits such as
:ref:`add_client() <class_NetwTestHarness_method_add_client>`,
:ref:`join_player() <class_NetwTestHarness_method_join_player>`,
:ref:`wait_for_scene() <class_NetwTestHarness_method_wait_for_scene>`, and
:ref:`wait_for_player() <class_NetwTestHarness_method_wait_for_player>`
through one timeout reporter. If the expected event does not happen, the
failure names the thing the test was waiting for.

:ref:`NetwGameHarness.sync_ticks() <class_NetwGameHarness_method_sync_ticks>`
advances the shared tree by network ticks. Use it after player input,
physics-driven motion, animation gates, or replication that is expected to
converge over time.

.. tabs::
 .. code-tab:: gdscript GDScript

    client.simulate_action_press("move_right")
    await game.sync_ticks(8)
    client.simulate_action_release("move_right")
    await game.sync_ticks(2)

.. warning::

   If the game uses :ref:`TPLayerAPI <class_TPLayerAPI>` transitions, wait
   with
   :ref:`wait_for_transition() <class_NetwGameHarness_method_wait_for_transition>`
   or
   :ref:`wait_for_transitions() <class_NetwGameHarness_method_wait_for_transitions>`.
   Those helpers wait for the transition state directly instead of relying
   on frame loops.

Network conditions
------------------

Link simulation is part of
:ref:`LocalLoopbackSession <class_LocalLoopbackSession>`, so it works with
:ref:`NetwTestHarness <class_NetwTestHarness>` and
:ref:`NetwGameHarness <class_NetwGameHarness>`. It does not change ENet,
WebRTC, WebSocket, or Steam sockets.

Use :ref:`degrade() <class_NetwGameHarness_method_degrade>` when a test talks
about one player's connection. It applies to both directions:

.. tabs::
 .. code-tab:: gdscript GDScript

    game.degrade(bob).profile(NetwLink.Profile.POOR_3G)
    game.degrade(bob).latency_ms(150).loss(0.03)

Use the direction filters when only one side of the exchange matters:

- :ref:`NetwLink.NetwLinkMulti.inbound() <class_NetwLink_NetwLinkMulti_method_inbound>`
  selects server-to-player traffic. It changes what that player receives.
- :ref:`NetwLink.NetwLinkMulti.outbound() <class_NetwLink_NetwLinkMulti_method_outbound>`
  selects player-to-server traffic. It changes when that player's actions
  reach the server.

.. tabs::
 .. code-tab:: gdscript GDScript

    game.degrade(bob).inbound().latency_ms(180)
    game.degrade(bob).outbound().loss(0.10)

Use :ref:`path() <class_NetwGameHarness_method_path>` when the packet flow
should be explicit. The arguments are ordered as sender, then receiver.

.. tabs::
 .. code-tab:: gdscript GDScript

    game.path(alice, bob).latency_ms(200)

Most game tests should set conditions in milliseconds or use a
:ref:`NetwLink.Profile <enum_NetwLink_Profile>`. Use
:ref:`exact() <class_NetwLink_method_exact>` only when the test needs an exact
deterministic plan. Exact plans expose
:ref:`LocalLoopbackSession.LinkPlan <class_LocalLoopbackSession_LinkPlan>`
in milliseconds.

.. tabs::
 .. code-tab:: gdscript GDScript

    game.path(alice, bob).exact() \
            .loss_prob(0.5) \
            .delay_ms(66.0) \
            .seed(1)

Real transports
---------------

:ref:`NetwTestHarness <class_NetwTestHarness>` uses
:ref:`LocalLoopbackBackend <class_LocalLoopbackBackend>` because it is fast
and deterministic. That also means it cannot test behavior that only exists
on real sockets, such as ENet addressing, bound ports, or the pre-game
:ref:`BackendPeer.query_server_info()
<class_BackendPeer_method_query_server_info>`
probe.

Use :ref:`EnetTestSupport <class_EnetTestSupport>` when the test needs real
UDP. It starts a host tree on an actual port and returns the address data a
client backend can probe.

.. tabs::
 .. code-tab:: gdscript GDScript

    func test_probe_returns_player_count() -> void:
        var host := await EnetTestSupport.start_host(self)
        var client_backend := EnetTestSupport.make_client_backend(host.port)

        var result: ServerInfoResult = await client_backend.query_server_info(
            "127.0.0.1",
            2.0
        )

        assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
        await EnetTestSupport.stop_tree(host.tree)


.. tip::

   Inside a test case, use
   :ref:`enable_logs() <class_NetwTestSuite_method_enable_logs>` for
   Networked logging and
   :ref:`enable_debugger() <class_NetwTestSuite_method_enable_debugger>` for
   reporter-backed traces.
