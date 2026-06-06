:allow_comments: False

.. _doc_manual_testing:

Testing
=======

Networked ships a small companion library in ``addons/networked_test``. It is
the same rig the addon uses for its own coverage, so anything you can read
here you can copy into a game project.

The two classes you use day to day are
:ref:`NetwTestSuite <class_NetwTestSuite>` as the GdUnit4 base, and
:ref:`NetwTestHarness <class_NetwTestHarness>` for multi peer flows. Register
:ref:`NetwTestSessionHook <class_NetwTestSessionHook>` in your GdUnit4
settings so debug state resets between tests and root node leaks are reported.

Writing a multiplayer test
--------------------------

A good multiplayer test reads like the game itself. Spin up a harness, add
peers, drive a flow, assert what the player sees. The harness owns peer
registration, scene mirroring, and teardown ordering. Harnesses created with
``make_harness()`` are torn down automatically after each test case.

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
        await harness.setup(NetwTestSuite.create_scene_manager)
        harness.register_spawnable_scene(LEVEL)
        alice = await harness.add_client()

    func test_spawn_assigns_authority() -> void:
        var player := harness.spawn_player(alice, PLAYER)
        await harness.wait_for_player(alice, &"TestLevel")
        assert_that(player.get_multiplayer_authority()).is_equal(
            alice.multiplayer_peer.get_unique_id()
        )

If your test reaches into private addon state to make a multiplayer flow
work, the harness is missing a public helper. Open an issue rather than
working around it. The test should not know about peer registration timing,
loopback transport setup, or frame drains.

If a test needs its own cleanup, call the base hook last:

.. tabs::
 .. code-tab:: gdscript GDScript

    func after_test() -> void:
        get_tree().paused = false
        await super.after_test()

Use ``make_unmanaged_harness()`` only when one test case intentionally needs
an extra harness. Unmanaged harnesses must be torn down explicitly.

Testing real game scenes
~~~~~~~~~~~~~~~~~~~~~~~~

Use ``make_game_harness(main_scene)`` when the test should adopt a real
game ``main.tscn`` instead of building trees from fixtures. The harness
mounts each participant in a ``ParticipantSlot``, promotes one instance to a
listen server, connects client instances through local loopback, and owns
``sync_ticks()`` for the shared clock.

Per participant input must enter through ``_unhandled_input``. Prefer
``InputComponent`` for player controls. Polling the global ``Input`` singleton
cannot be scoped to one slot, so it is unsupported for slot routed game tests.

.. tabs::
 .. code-tab:: gdscript GDScript

    class_name TestDailyTwoPlayers
    extends NetwTestSuite

    const MAIN := preload("res://examples/daily/Main.tscn")

    var game: NetwGameHarness

    func before_test() -> void:
        game = make_game_harness(MAIN)
        await game.setup()

    func test_bob_sees_alice_move() -> void:
        var alice := await game.add_host("alice")
        var bob := await game.add_client("bob")

        var alice_on_bob: Node2D = await bob.await_player(&"alice")
        var start := alice_on_bob.position.x

        alice.simulate_action_press("move_right")
        await game.sync_ticks(16)
        alice.simulate_action_release("move_right")
        await game.sync_ticks(16)

        assert_that(alice_on_bob.position.x).is_greater(start)

Transport-specific tests
~~~~~~~~~~~~~~~~~~~~~~~~

:ref:`NetwTestHarness <class_NetwTestHarness>` is built around
:ref:`LocalLoopbackBackend <class_LocalLoopbackBackend>` and does not
generalize to real transports. For tests that need real UDP sockets --
exercising the auth-phase handshake behind
:ref:`query_server_info() <class_BackendPeer_method_query_server_info>`,
ENet-level disconnect semantics, port-aware addressing -- use
:ref:`EnetTestSupport <class_EnetTestSupport>` instead. The two helpers
are complementary, not composable; pick the one whose contract matches
the unit under test.

.. tabs::
 .. code-tab:: gdscript GDScript

    func test_probe_returns_player_count() -> void:
        var host := await EnetTestSupport.start_host(self)
        var client_backend := EnetTestSupport.make_client_backend(host.port)

        var result: ServerInfoResult = await client_backend.query_server_info(
            "127.0.0.1", 2.0
        )
        assert_int(result.status).is_equal(ServerInfoResult.Status.OK)

        await EnetTestSupport.stop_tree(host.tree)

Three categories of test
------------------------

The line between a sample a game author would copy and coverage written by
an addon maintainer matters. Keep the three categories visible.

- Public SDK examples. Use only
  :ref:`NetwTestSuite <class_NetwTestSuite>` and
  :ref:`NetwTestHarness <class_NetwTestHarness>` on the public side. No
  private methods, no fixtures outside ``addons/networked_test/fixtures``.
  These are the tests a user can copy.
- Addon integration tests. Allowed to touch addon internals when the
  scenario demands it. Mark them with a region or a class doc comment so
  the reader knows why.
- Internal algorithm tests. May call private methods such as
  ``_calibrate()`` on the
  unit under test. The file structure should make the intent obvious. Use
  Godot code regions to fence them off.

.. tabs::
 .. code-tab:: gdscript GDScript

    #region Public contract
    ...
    #endregion

    #region Internal algorithm
    ...
    #endregion

Tables and oracles
------------------

For deterministic APIs, a parameter table is almost always clearer than a
stack of near identical test functions. Each row reads as one row of the
specification, and the failing row names the case.

.. tabs::
 .. code-tab:: gdscript GDScript

    func test_display_tick_with_offset(
        tick: int,
        offset: int,
        expected: int,
        test_parameters := [
            [10, 0, 10],
            [10, 3, 7],
            [2,  5, 0],
        ],
    ) -> void:
        var clock := _make_clock()
        clock.display_offset = offset
        clock.tick = tick
        assert_that(clock.display_tick).is_equal(expected)

When a structure has more states than examples can cover, run a simple
reference model next to the implementation and assert they agree. The
oracle should be deliberately boring. A plain array or a linear search is
the right tool. Name the test after the invariant it protects, not the
mechanism.

.. tabs::
 .. code-tab:: gdscript GDScript

    var buf := HistoryBuffer.new(4)
    var oracle: Array = []

    for i in 12:
        var tick := base_tick + i
        var value := "value_%d" % tick
        buf.record(tick, value)
        oracle.append([tick, value])
        if oracle.size() > 4:
            oracle.pop_front()

    assert_that(buf.oldest_tick()).is_equal(oracle.front()[0])
    assert_that(buf.newest_tick()).is_equal(oracle.back()[0])

Awaits and timeouts
-------------------

User facing tests should not poll frames. The harness wraps every wait
through a single timeout reporter, so calls such as
:ref:`add_client() <class_NetwTestHarness_method_add_client>`,
:ref:`join_player() <class_NetwTestHarness_method_join_player>`, and
:ref:`wait_for_player() <class_NetwTestHarness_method_wait_for_player>`
fail the test cleanly when the expected event does not arrive.

Outside GdUnit4, assign your own reporter to
:ref:`awaiter <class_NetwTestHarness_property_awaiter>`. The harness core
is framework agnostic, the GdUnit4 binding is just one adapter.
