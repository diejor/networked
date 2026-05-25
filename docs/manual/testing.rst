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
registration, scene mirroring, and teardown ordering, so your test never
names them.

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

    func after_test() -> void:
        await harness.teardown()

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
  :ref:`_calibrate() <class_NetworkClock_private_method__calibrate>` on the
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
