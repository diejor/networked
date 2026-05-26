## Unit tests for [NetworkClock].
##
## Covers derived properties (public contract), the snap/stretch calibration
## paths, the [code]clock_synchronized[/code] signal, [method NetworkClock.for_node]
## lookup, and the physics-process tick loop in isolation. The calibration
## and tick-loop tests are addon-internal coverage that intentionally call
## private methods on the unit under test.
class_name TestNetworkClock
extends NetwTestSuite


func _make_clock(tickrate: int = 30) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.tickrate = tickrate
	return auto_free(clock)


func _make_clock_in_tree(
	tickrate: int = 10,
	use_physics_interpolation: bool = true,
) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.tickrate = tickrate
	clock.use_physics_interpolation = use_physics_interpolation
	add_child(clock)
	auto_free(clock)
	return clock


#region Public properties

func test_ticktime_is_reciprocal_of_tickrate(
	tickrate: int,
	test_parameters := [[20], [60]],
) -> void:
	var clock := _make_clock(tickrate)
	assert_that(absf(clock.ticktime - 1.0 / tickrate) < 0.0001).is_true()


func test_display_tick_with_offset(
	tick: int,
	offset: int,
	expected: int,
	test_parameters := [
		[10, 0, 10],   # zero offset -> identity
		[10, 3, 7],    # positive offset lags
		[2,  5, 0],    # clamped at zero when tick < offset
	],
) -> void:
	var clock := _make_clock()
	clock.display_offset = offset
	clock.tick = tick
	assert_that(clock.display_tick).is_equal(expected)


#endregion

#region Internal calibration algorithm

# SNAP jumps directly to the target regardless of the magnitude or sign of
# the diff.
func test_calibrate_snap_always_jumps(
	starting_tick: int,
	target_tick: int,
	test_parameters := [
		[0,   50],
		[100, 200],
		[100, 80],
		[10,  11],
	],
) -> void:
	var clock := _make_clock()
	clock.sync_mode = NetworkClock.SyncMode.SNAP
	clock.tick = starting_tick
	clock._calibrate(target_tick)
	assert_that(clock.tick).is_equal(target_tick)


# STRETCH below the panic threshold nudges the accumulator instead of
# jumping. The tick value stays put and the accumulator goes positive.
func test_calibrate_stretch_nudges_small_diffs() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = NetworkClock.SyncMode.STRETCH
	clock.panic_snap_threshold = 100
	clock.tick = 10
	clock._tick_accumulator = 0.0

	clock._calibrate(12)

	assert_that(clock.tick).is_equal(10)
	assert_that(clock._tick_accumulator > 0.0).is_true()


# STRETCH falls back to a hard snap when the diff exceeds the panic
# threshold.
func test_calibrate_panic_snaps_above_threshold() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = NetworkClock.SyncMode.STRETCH
	clock.panic_snap_threshold = 5
	clock.tick = 10

	clock._calibrate(20)

	assert_that(clock.tick).is_equal(20)


# clock_synchronized fires exactly once on the first calibration. Subsequent
# calls flip [is_synchronized] true but emit nothing further.
func test_calibrate_emits_synchronized_signal_once() -> void:
	var clock := _make_clock()
	var counter := SignalCounter.watch(clock.clock_synchronized)

	assert_that(clock.is_synchronized).is_false()

	clock._calibrate(1)
	assert_that(clock.is_synchronized).is_true()
	assert_that(counter.count).is_equal(1)

	clock._calibrate(2)
	clock._calibrate(3)
	assert_that(counter.count).is_equal(1)


#endregion

#region Clock lookup

func test_for_node_returns_null_when_no_clock_registered() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(NetworkClock.for_node(node)).is_null()


# for_node walks the node's multiplayer API and reads the registered clock
# from a meta key. The SceneTree always provides a SceneMultiplayer at the
# root in a test environment, so assert that precondition rather than
# silently skipping when the cast fails.
func test_for_node_returns_registered_clock() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)

	var api := node.multiplayer as SceneMultiplayer
	assert_that(api).is_not_null()

	var clock := _make_clock()
	api.set_meta(&"_network_clock", clock)

	assert_that(NetworkClock.for_node(node)).is_same(clock)

	api.remove_meta(&"_network_clock")


#endregion

#region Internal tick loop

func test_physics_process_tick_advancement(
	delta: float,
	expected_advance: int,
	test_parameters := [
		[0.05, 0],   # below ticktime -> no advance
		[0.10, 1],   # exactly one ticktime -> one advance
	],
) -> void:
	var clock := _make_clock_in_tree(10)
	var start_tick := clock.tick
	clock._physics_process(delta)
	assert_that(clock.tick).is_equal(start_tick + expected_advance)


# tick_factor exposes the fractional position within the current ticktime.
# The interpolation-off branch uses wall-clock time since the last physics
# frame, which is deterministic for synthetic _physics_process calls and is
# the only path testable without a real physics step.
func test_physics_process_tick_factor_without_interpolation(
	delta: float,
	expected_factor: float,
	test_parameters := [
		[0.10, 0.0],   # exact tick boundary -> factor is 0
		[0.15, 0.5],   # half a ticktime extra -> factor is 0.5
	],
) -> void:
	var clock := _make_clock_in_tree(10, false)
	clock._physics_process(delta)
	assert_that(clock.tick_factor).is_equal_approx(expected_factor, 0.01)


# on_tick fires once per advanced tick, up to [max_ticks_per_frame].
func test_on_tick_emit_count(
	delta: float,
	max_ticks: int,
	expected_count: int,
	test_parameters := [
		[0.35, 100, 3],   # uncapped, 3.5 ticktimes -> 3 ticks
		[1.0,  2,   2],   # cap clamps a 10-tick delta to 2
	],
) -> void:
	var clock := _make_clock_in_tree(10)
	clock.max_ticks_per_frame = max_ticks

	var counter := SignalCounter.watch(clock.on_tick)
	clock._physics_process(delta)
	assert_that(counter.count).is_equal(expected_count)

#endregion
