## Unit tests for the [NetwClockInterface] tick engine and the
## [MultiplayerClock] configurator node.
##
## Covers derived properties, calibration paths,
## [signal NetwClockInterface.clock_synchronized],
## [method MultiplayerClock.for_node], and the isolated tick loop.
class_name TestMultiplayerClock
extends NetwTestSuite

func _make_clock(tickrate: int = 30) -> NetwClockInterface:
	var clock := NetwClockInterface.new()
	clock._configured = true
	clock.tickrate = tickrate
	return clock


func _make_engine(
		tickrate: int = 10,
		use_physics_interpolation: bool = true,
) -> NetwClockInterface:
	var clock := NetwClockInterface.new()
	clock._configured = true
	clock.tickrate = tickrate
	clock.use_physics_interpolation = use_physics_interpolation
	return clock


func test_public_derived_properties() -> void:
	for tickrate in [20, 60]:
		var clock := _make_clock(tickrate)
		assert_that(absf(clock.ticktime - 1.0 / tickrate) < 0.0001).is_true()

	for row in [
		[10, 0, 10],
		[10, 3, 7],
		[2, 5, 0],
	]:
		var clock := _make_clock()
		clock.display_offset = row[1]
		clock.tick = row[0]
		assert_that(clock.display_tick).is_equal(row[2])


func test_calibration_modes() -> void:
	for row in [
		[0, 50],
		[100, 200],
		[100, 80],
		[10, 11],
	]:
		var clock := _make_clock()
		clock.sync_mode = NetwClockInterface.SyncMode.SNAP
		clock.is_synchronized = true
		clock.tick = row[0]
		clock._calibrate(row[1])
		assert_that(clock.tick).is_equal(row[1])

	var stretch := _make_clock(30)
	stretch.sync_mode = NetwClockInterface.SyncMode.STRETCH
	stretch.is_synchronized = true
	stretch.tick = 10
	stretch._tick_accumulator = 0.0

	stretch._calibrate(12)

	assert_that(stretch.tick).is_equal(10)
	assert_that(stretch._target_tick_estimate).is_equal_approx(12.0, 0.0001)


func test_stretch_nudge_and_synchronization_signal() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = NetwClockInterface.SyncMode.STRETCH
	clock.is_synchronized = true
	clock.stretch_nudge_factor = 0.5
	clock.panic_snap_threshold = 100
	clock.tick = 10
	clock._tick_accumulator = 0.0
	clock._target_tick_estimate = 12.0

	clock._nudge_toward_estimate()

	assert_that(clock.tick).is_equal(10)
	assert_that(clock._tick_accumulator).is_equal_approx(clock.ticktime, 0.0001)

	clock.panic_snap_threshold = 5
	clock._target_tick_estimate = 30.0

	clock._nudge_toward_estimate()

	assert_that(clock.tick).is_equal(30)

	var first_sync := _make_clock()
	var counter := SignalCounter.watch(first_sync.clock_synchronized)

	assert_that(first_sync.is_synchronized).is_false()

	first_sync._calibrate(1)
	first_sync._calibrate(2)
	first_sync._calibrate(3)

	assert_that(first_sync.is_synchronized).is_true()
	assert_that(counter.count).is_equal(1)


# The calibration target tracks the server's continuous clock position, so a
# ping arriving a fraction of a tick later moves the target by that fraction and
# never by a whole tick. A quantized anchor would instead flip by one tick as the
# arrival phase crossed a server tick boundary, and STRETCH would spend about a
# second chasing each flip, sweeping the client's tick boundary through the
# server's consume boundary as it went.
func test_pong_target_follows_the_server_phase_continuously() -> void:
	var targets: Array[float] = []
	for phase in [0.0, 0.25, 0.5, 0.75]:
		var clock := _make_clock(30)
		clock.sync_mode = NetwClockInterface.SyncMode.STRETCH
		clock.lead_ticks = 1.0
		clock.is_synchronized = true
		clock.tick = 100

		clock.handle_pong(0.0, 100, phase)
		targets.append(clock._target_tick_estimate)

	# Zero RTT and a lead of one tick put the anchor exactly one tick ahead of
	# the server, and each phase step carries it forward by that same fraction.
	for i in targets.size():
		assert_float(targets[i]) \
			.override_failure_message(
				"phase %.2f anchored at %.4f" % [i * 0.25, targets[i]],
			).is_equal_approx(101.0 + i * 0.25, 0.01)


# A pong seeds both the tick counter and the phase within it, so a first
# calibration lands the local clock on the server's position rather than on the
# tick boundary below it.
func test_first_calibration_seeds_the_intra_tick_phase() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = NetwClockInterface.SyncMode.STRETCH

	clock._calibrate(40.5)

	assert_that(clock.tick).is_equal(40)
	assert_float(clock._tick_accumulator) \
		.is_equal_approx(clock.ticktime * 0.5, 0.0001)


func test_for_node_lookup() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(MultiplayerClock.for_node(node)).is_null()

	var api := node.multiplayer
	assert_that(api).is_not_null()

	var clock := MultiplayerClock.new()
	auto_free(clock)
	api.set_meta(&"_multiplayer_clock", clock)

	assert_that(MultiplayerClock.for_node(node)).is_same(clock)

	api.remove_meta(&"_multiplayer_clock")


func test_physics_step_tick_loop() -> void:
	for row in [
		[0.05, 0],
		[0.10, 1],
	]:
		var clock := _make_engine(10)
		var start_tick := clock.tick
		clock.physics_step(row[0])
		assert_that(clock.tick).is_equal(start_tick + row[1])

	for row in [
		[0.10, 0.0],
		[0.15, 0.5],
	]:
		var clock := _make_engine(10, false)
		clock.physics_step(row[0])
		assert_that(clock.tick_factor).is_equal_approx(row[1], 0.01)

	for row in [
		[0.35, 100, 3],
		[1.0, 2, 2],
	]:
		var clock := _make_engine(10)
		clock.max_ticks_per_frame = row[1]

		var counter := SignalCounter.watch(clock.on_tick)
		clock.physics_step(row[0])
		assert_that(counter.count).is_equal(row[2])
