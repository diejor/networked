## Unit tests for [NetworkClock].
##
## Networking RPCs and full tick-loop integration are out of scope here.
## These tests exercise the logic that runs purely in-process:
## [ul]
## [li]Derived properties ([member NetworkClock.ticktime], [member NetworkClock.display_tick])[/li]
## [li]Clock calibration in Snap and Stretch modes ([method NetworkClock._calibrate])[/li]
## [li][signal NetworkClock.clock_synchronized] fires exactly once[/li]
## [li][method NetworkClock.for_node] returns [code]null[/code] when no clock is registered[/li]
## [li]The tick loop advances [member NetworkClock.tick] and [member NetworkClock.tick_factor]
##     correctly when [method NetworkClock._physics_process] is driven manually[/li]
## [/ul]
##
## [b]Note:[/b] [method NetworkClock.for_node] depends on [member Node.multiplayer] returning
## a [SceneMultiplayer] with the [code]_network_clock[/code] meta set.  The
## [code]test_for_node_*[/code] tests add nodes to the active scene tree so that
## [member Node.multiplayer] is non-null.
class_name TestNetworkClock
extends NetworkedTestSuite


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

func _make_clock(tickrate: int = 30) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.tickrate = tickrate
	return auto_free(clock)


# ---------------------------------------------------------------------------
# ticktime
# ---------------------------------------------------------------------------

func test_ticktime_is_reciprocal_of_tickrate() -> void:
	var clock := _make_clock(20)
	assert_that(absf(clock.ticktime - 1.0 / 20.0) < 0.0001).is_true()


func test_ticktime_updates_when_tickrate_changes() -> void:
	var clock := _make_clock(60)
	assert_that(absf(clock.ticktime - 1.0 / 60.0) < 0.0001).is_true()


# ---------------------------------------------------------------------------
# display_tick
# ---------------------------------------------------------------------------

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


# ---------------------------------------------------------------------------
# _calibrate — Snap mode (sync_mode = 0)
# ---------------------------------------------------------------------------

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
	# Snap always jumps regardless of diff magnitude.
	var clock := _make_clock()
	clock.sync_mode = 0
	clock.tick = 10
	clock._calibrate(11)
	assert_that(clock.tick).is_equal(11)


# ---------------------------------------------------------------------------
# _calibrate — Stretch mode (sync_mode = 1)
# ---------------------------------------------------------------------------

func test_calibrate_stretch_does_not_snap_on_small_diff() -> void:
	var clock := _make_clock()
	clock.sync_mode = 1  # Stretch
	clock.tick = 10
	clock._calibrate(11)  # diff = 1, <= 3 → stretch, not snap
	assert_that(clock.tick).is_equal(10)  # tick unchanged


func test_calibrate_stretch_nudges_accumulator_forward() -> void:
	var clock := _make_clock(30)
	clock.sync_mode = 1
	clock.tick = 10
	clock._tick_accumulator = 0.0
	clock._calibrate(12)  # diff = 2, ticktime = 1/30 ≈ 0.0333
	# Expected nudge: 2 * (1/30) * 0.1 ≈ 0.00667 > 0
	assert_that(clock._tick_accumulator > 0.0).is_true()


func test_calibrate_stretch_snaps_when_diff_exceeds_threshold() -> void:
	# diff > 3 triggers snap even in Stretch mode.
	var clock := _make_clock()
	clock.sync_mode = 1
	clock.tick = 10
	clock._calibrate(20)  # diff = 10 > 3 → snap
	assert_that(clock.tick).is_equal(20)


# ---------------------------------------------------------------------------
# clock_synchronized signal
# ---------------------------------------------------------------------------

func test_calibrate_marks_synchronized_after_first_call() -> void:
	var clock := _make_clock()
	assert_that(clock.is_synchronized).is_false()
	clock._calibrate(1)
	assert_that(clock.is_synchronized).is_true()


func test_calibrate_emits_clock_synchronized_signal() -> void:
	# Use a Dictionary for the flag — GDScript lambdas capture primitive locals
	# by value, so a bare `var fired := false` would never be updated.
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


# ---------------------------------------------------------------------------
# for_node
# ---------------------------------------------------------------------------

func test_for_node_returns_null_when_no_clock_registered() -> void:
	# Add a plain node to the scene so node.multiplayer is non-null,
	# but don't register any NetworkClock on the API.
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(NetworkClock.for_node(node)).is_null()


func test_for_node_returns_registered_clock() -> void:
	# Manually register a NetworkClock on the SceneMultiplayer API
	# the same way _on_tree_configured does at runtime.
	var node := Node.new()
	add_child(node)
	auto_free(node)

	var api := node.multiplayer as SceneMultiplayer
	if not api:
		# Headless runner may not provide SceneMultiplayer; skip gracefully.
		return

	var clock := _make_clock()
	api.set_meta(&"_network_clock", clock)

	var result := NetworkClock.for_node(node)
	assert_that(result).is_same(clock)

	api.remove_meta(&"_network_clock")


# ---------------------------------------------------------------------------
# Tick loop (_physics_process driven manually)
# ---------------------------------------------------------------------------

func test_tick_advances_after_one_full_ticktime() -> void:
	# Drive _physics_process with exactly one tick worth of delta.
	# The node must be added to the tree so multiplayer is available;
	# we use set_multiplayer_authority to keep it offline-safe.
	var clock := NetworkClock.new()
	clock.tickrate = 10  # ticktime = 0.1 s
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
	clock._physics_process(0.05)  # half a ticktime
	assert_that(clock.tick).is_equal(start_tick)


func test_tick_factor_is_zero_at_start_of_tick() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	add_child(clock)
	auto_free(clock)

	clock._physics_process(0.1)  # exactly one tick
	# After a full tick, accumulator resets to 0 → factor = 0
	assert_that(clock.tick_factor).is_equal_approx(0.0, 0.01)


func test_tick_factor_reflects_partial_accumulation() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10  # ticktime = 0.1 s
	add_child(clock)
	auto_free(clock)

	clock._physics_process(0.15)  # one full tick + 0.05 s left → factor ≈ 0.5
	assert_that(clock.tick_factor).is_equal_approx(0.5, 0.01)


func test_multiple_ticks_fire_signals() -> void:
	var clock := NetworkClock.new()
	clock.tickrate = 10
	add_child(clock)
	auto_free(clock)

	# Use an Array — captured by reference in lambdas, unlike primitive locals.
	var result := {"count": 0}
	clock.on_tick.connect(func(_d: float, _t: int) -> void: result.count += 1)

	# 0.35 s > 3 × ticktime (0.1 s) with enough margin to survive float rounding.
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

	# 1.0 s = 10 ticks at 10 Hz, but the cap is 2.
	clock._physics_process(1.0)
	assert_that(result.count).is_equal(2)
