## Unit tests for [NetworkClock].
##
## Exercises derived properties, calibration, and the tick loop in isolation.
class_name TestNetworkClock
extends NetworkedTestSuite


func _make_clock(tickrate: int = 30) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.tickrate = tickrate
	return auto_free(clock)


func test_ticktime_is_reciprocal_of_tickrate() -> void:
	var clock := _make_clock(20)
	assert_that(absf(clock.ticktime - 1.0 / 20.0) < 0.0001).is_true()


func test_ticktime_updates_when_tickrate_changes() -> void:
	var clock := _make_clock(60)
	assert_that(absf(clock.ticktime - 1.0 / 60.0) < 0.0001).is_true()


func test_display_tick_equals_tick_when_offset_is_zero() -> void:
	var clock := _make_clock()
	clock.display_offset = 0
	clock.tick = 10
	assert_that(clock.display_tick).is_equal(10)


func test_display_tick_lags_by_offset() -> void:
	var clock := _make_clock()
	clock.display_offset = 3
	clock.tick = 10
	assert_that(clock.display_tick).is_equal(7)


func test_display_tick_clamped_at_zero_when_tick_less_than_offset() -> void:
	var clock := _make_clock()
	clock.display_offset = 5
	clock.tick = 2
	assert_that(clock.display_tick).is_equal(0)


func test_calibrate_snap_jumps_to_target() -> void:
	var clock := _make_clock()
	clock.sync_mode = 0  # Snap
	clock.tick = 0
	clock._calibrate(50)
	assert_that(clock.tick).is_equal(50)


func test_calibrate_snap_on_large_positive_diff_jumps() -> void:
	var clock := _make_clock()
	clock.sync_mode = 0
	clock.tick = 100
	clock._calibrate(200)
	assert_that(clock.tick).is_equal(200)


func test_calibrate_snap_on_large_negative_diff_jumps() -> void:
	var clock := _make_clock()
	clock.sync_mode = 0
	clock.tick = 100
	clock._calibrate(80)
	assert_that(clock.tick).is_equal(80)


func test_calibrate_snap_forces_jump_even_for_small_diff() -> void:
	var clock := _make_clock()
	clock.sync_mode = 0
	clock.tick = 10
	clock._calibrate(11)
	assert_that(clock.tick).is_equal(11)


func test_calibrate_stretch_does_not_snap_on_small_diff() -> void:
	var clock := _make_clock()
	clock.sync_mode = 1  # Stretch
	clock.tick = 10
	clock._calibrate(11)
	assert_that(clock.tick).is_equal(10)


func test_calibrate_stretch_nudges_accumulator_forward() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = 1
	clock.tick = 10
	clock._tick_accumulator = 0.0
	clock._calibrate(12)
	assert_that(clock._tick_accumulator > 0.0).is_true()


func test_calibrate_stretch_snaps_when_diff_exceeds_threshold() -> void:
	var clock := _make_clock()
	clock.sync_mode = 1
	clock.tick = 10
	clock.panic_snap_threshold = 5
	clock._calibrate(20)
	assert_that(clock.tick).is_equal(20)


func test_calibrate_marks_synchronized_after_first_call() -> void:
	var clock := _make_clock()
	assert_that(clock.is_synchronized).is_false()
	clock._calibrate(1)
	assert_that(clock.is_synchronized).is_true()


func test_calibrate_emits_clock_synchronized_signal() -> void:
	var clock := _make_clock()
	var result := {"fired": false}
	clock.clock_synchronized.connect(func() -> void: result.fired = true)
	clock._calibrate(1)
	assert_that(result.fired).is_true()


func test_calibrate_emits_synchronized_signal_only_once() -> void:
	var clock := _make_clock()
	var result := {"count": 0}
	clock.clock_synchronized.connect(func() -> void: result.count += 1)
	clock._calibrate(1)
	clock._calibrate(2)
	clock._calibrate(3)
	assert_that(result.count).is_equal(1)


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

	var result := NetworkClock.for_node(node)
	assert_that(result).is_same(clock)

	api.remove_meta(&"_network_clock")


func test_tick_advances_after_one_full_ticktime() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	add_child(clock)
	auto_free(clock)

	var start_tick := clock.tick
	clock._physics_process(0.1)
	assert_that(clock.tick).is_equal(start_tick + 1)


func test_tick_does_not_advance_on_partial_delta() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	add_child(clock)
	auto_free(clock)

	var start_tick := clock.tick
	clock._physics_process(0.05)
	assert_that(clock.tick).is_equal(start_tick)


func test_tick_factor_is_zero_at_start_of_tick() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	clock.use_physics_interpolation = false
	add_child(clock)
	auto_free(clock)

	clock._physics_process(0.1)
	assert_that(clock.tick_factor).is_equal_approx(0.0, 0.01)


func test_tick_factor_reflects_partial_accumulation() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	clock.use_physics_interpolation = false
	add_child(clock)
	auto_free(clock)

	clock._physics_process(0.15)
	assert_that(clock.tick_factor).is_equal_approx(0.5, 0.01)


func test_multiple_ticks_fire_signals() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	add_child(clock)
	auto_free(clock)

	var result := {"count": 0}
	clock.on_tick.connect(func(_d: float, _t: int) -> void: result.count += 1)

	clock._physics_process(0.35)
	assert_that(result.count).is_equal(3)


func test_max_ticks_per_frame_caps_loop() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	clock.max_ticks_per_frame = 2
	add_child(clock)
	auto_free(clock)

	var result := {"count": 0}
	clock.on_tick.connect(func(_d: float, _t: int) -> void: result.count += 1)

	clock._physics_process(1.0)
	assert_that(result.count).is_equal(2)
