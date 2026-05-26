## Unit tests for [NetworkClock].
##
## Covers derived properties, the snap/stretch calibration paths, the
## first-sync signal, [method NetworkClock.for_node] lookup, and the
## physics-process tick loop in isolation.
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


# region: derived properties --------------------------------------------------

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


# region: calibration ---------------------------------------------------------

# Snap mode (sync_mode = 0) jumps directly to the target regardless of the
# magnitude or sign of the diff.
func test_calibrate_snap_always_jumps(
	starting_tick: int,
	target_tick: int,
	test_parameters := [
		[0,   50],    # large positive from zero
		[100, 200],   # large positive
		[100, 80],    # large negative
		[10,  11],    # small diff -> still snaps (no stretch protection)
	],
) -> void:
	var clock := _make_clock()
	clock.sync_mode = 0
	clock.tick = starting_tick
	clock._calibrate(target_tick)
	assert_that(clock.tick).is_equal(target_tick)


# Stretch mode below panic threshold nudges the accumulator instead of
# jumping; above the threshold it falls back to a hard snap.
func test_calibrate_stretch_behaviour(
	diff: int,
	threshold: int,
	expects_snap: bool,
	test_parameters := [
		[2,  100, false],   # small diff, well under threshold -> nudge
		[10, 5,   true],    # diff exceeds threshold -> panic-snap
	],
) -> void:
	var clock := _make_clock(30)
	clock.sync_mode = 1
	clock.panic_snap_threshold = threshold
	clock.tick = 10
	clock._tick_accumulator = 0.0
	var target := clock.tick + diff

	clock._calibrate(target)

	if expects_snap:
		assert_that(clock.tick).is_equal(target)
	else:
		assert_that(clock.tick).is_equal(10)
		assert_that(clock._tick_accumulator > 0.0).is_true()


# clock_synchronized fires exactly once: on the first calibration call.
# Subsequent calls flip [is_synchronized] true but emit nothing further.
func test_calibrate_emits_synchronized_signal_once() -> void:
	var clock := _make_clock()
	var count := [0]
	clock.clock_synchronized.connect(func() -> void: count[0] += 1)

	assert_that(clock.is_synchronized).is_false()

	clock._calibrate(1)
	assert_that(clock.is_synchronized).is_true()
	assert_that(count[0]).is_equal(1)

	clock._calibrate(2)
	clock._calibrate(3)
	assert_that(count[0]).is_equal(1)


# region: for_node lookup -----------------------------------------------------

func test_for_node_returns_null_when_no_clock_registered() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(NetworkClock.for_node(node)).is_null()


func test_for_node_returns_registered_clock() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)

	var api := node.multiplayer as SceneMultiplayer
	if not api:
		return

	var clock := _make_clock()
	api.set_meta(&"_network_clock", clock)

	assert_that(NetworkClock.for_node(node)).is_same(clock)

	api.remove_meta(&"_network_clock")


# region: physics_process tick loop -------------------------------------------

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


func test_physics_process_tick_factor(
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

	var count := [0]
	clock.on_tick.connect(func(_d: float, _t: int) -> void: count[0] += 1)

	clock._physics_process(delta)
	assert_that(count[0]).is_equal(expected_count)
