## Unit tests for [TickInterpolator].
##
## All timing is driven manually — no real physics loop or network stack needed.
## The test controls exactly which ticks fire and when process frames run,
## making assertions on [member Node2D.position] deterministic.
##
## Terminology used in this file:
## [ul]
## [li][b]tick[/b] — a call to [method NetworkClock._physics_process] that advances the clock by one ticktime.[/li]
## [li][b]network update[/b] — directly setting [member Node2D.position] on the player, simulating what a [MultiplayerSynchronizer] does on a remote peer.[/li]
## [li][b]process frame[/b] — calling [method TickInterpolator._process] manually.[/li]
## [/ul]
##
## Key invariant that every test verifies one facet of:
## [br]
## After [param display_offset] ticks, the displayed [member Node2D.position] must smoothly
## interpolate between the last two distinct network snapshots bracketing [member NetworkClock.display_tick].
class_name TestTickInterpolator
extends NetworkedTestSuite


const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)


var _player: Node2D
var _clock: NetworkClock
var _interpolator: TickInterpolator


# ---------------------------------------------------------------------------
# Setup / teardown
# ---------------------------------------------------------------------------

func before_test() -> void:
	# Clock — manually driven, not connected to the physics loop.
	_clock = NetworkClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0  # display_tick = clock.tick; easier to reason about in tests
	add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)

	var api := _clock.multiplayer as SceneMultiplayer
	assert(api != null, "test requires SceneMultiplayer")
	api.set_meta(&"_network_clock", _clock)

	# Remote player — authority 999 ≠ local peer 1, so is_multiplayer_authority() = false.
	_player = Node2D.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(999)
	add_child(_player)
	auto_free(_player)

	# Synchronizer — required so _get_configuration_warnings() doesn't flag missing tracker.
	var sync := MultiplayerSynchronizer.new()
	var cfg := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(ppath, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	sync.replication_config = cfg
	_player.add_child(sync)

	# Interpolator — get_parent() resolves to _player at runtime, no owner needed.
	_interpolator = TickInterpolator.new()
	_interpolator.properties = [&"position"]
	_interpolator.enable_smart_dilation = false
	_player.add_child(_interpolator)
	_interpolator.set_process(false)  # manually driven

	await get_tree().process_frame


func after_test() -> void:
	var api := _clock.multiplayer as SceneMultiplayer
	if api and api.has_meta(&"_network_clock"):
		api.remove_meta(&"_network_clock")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

## Advances the clock by one tick, which fires [signal NetworkClock.after_tick]
## and therefore [method TickInterpolator._record_tick].
func _tick() -> void:
	_clock._physics_process(_clock.ticktime)


## Simulates a network packet: sets [member Node2D.position] then runs a process frame
## so the snapshot is captured before the next tick.
func _network_update(pos: Vector2) -> void:
	_player.position = pos
	_interpolator._process(0.0)


# ---------------------------------------------------------------------------
# Authority guard
# ---------------------------------------------------------------------------

func test_authority_player_position_is_not_modified_by_process() -> void:
	## _process must be a no-op when the owner is the local multiplayer authority,
	## otherwise the interpolator fights with the player's own _physics_process.
	_player.set_multiplayer_authority(_player.multiplayer.get_unique_id())

	_network_update(P0)
	_tick()
	_network_update(P1)
	_tick()

	_player.position = P1  # authority set this via move_and_slide
	_interpolator._process(0.016)

	assert_vector(_player.position).is_equal(P1)


# ---------------------------------------------------------------------------
# Empty buffer
# ---------------------------------------------------------------------------

func test_empty_buffer_leaves_position_unchanged() -> void:
	## With no ticks fired, the buffer is empty.
	## _process must not touch the position.
	_player.position = P0
	_interpolator._process(0.0)
	assert_vector(_player.position).is_equal(P0)


# ---------------------------------------------------------------------------
# Feedback-loop isolation
# ---------------------------------------------------------------------------

func test_buffer_records_network_value_not_interpolated_value() -> void:
	## This is the core regression test.
	##
	## _process writes a lerped value to owner.position.
	## _record_tick must NOT read that lerped value back into the buffer —
	## it must read the latest network snapshot instead.
	##
	## Failure here means the buffer feeds back its own output, which causes
	## the displayed position to drift away from the actual remote position.

	_network_update(P0)
	_tick()  # buf records P0 at tick 0; clock.tick → 1
	_tick()  # buf records P0 again (sparse: skipped); clock.tick → 2

	# Run several render frames — _process writes lerp results to player.position.
	for _i in 5:
		_interpolator._process(0.016)

	# Now a network packet arrives with P1.
	_network_update(P1)
	_tick()  # buf should record P1 at tick 2, NOT whatever _process last wrote

	var buf: HistoryBuffer = _interpolator._buffers[&"position"]
	assert_that(buf.get_at(2)).is_equal(P1)


# ---------------------------------------------------------------------------
# Interpolation correctness
# ---------------------------------------------------------------------------

func test_position_is_at_p0_before_any_update() -> void:
	## Before the second snapshot arrives, the interpolator can only show P0.
	_network_update(P0)
	_tick()  # buf: {0: P0}

	_clock.tick_factor = 0.5
	_interpolator._process(0.0)

	assert_vector(_player.position).is_equal_approx(P0, Vector2(0.1, 0.1))


func test_position_reaches_p1_after_update_tick() -> void:
	## At display_tick = 8 (= the P1 update tick) and factor = 1.0,
	## the position must equal P1 exactly.
	const UPDATE_TICK := 8
	_clock.display_offset = UPDATE_TICK  # ensures both snapshots are in buffer when displayed

	_network_update(P0)
	_tick()  # tick 0: P0 recorded

	for _i in UPDATE_TICK - 1:
		_tick()  # ticks 1-7: snapshot unchanged, not re-recorded

	_network_update(P1)
	_tick()  # tick 8: P1 recorded; clock.tick → 9

	# display_tick = clock.tick - display_offset = 9 - 8 = 1, not ideal for this test.
	# Drive forward until display_tick = UPDATE_TICK.
	for _i in UPDATE_TICK - 1:
		_tick()

	# Now clock.tick = 18, display_tick = 10. Overshoot is fine — clamped to P1.
	_clock.tick_factor = 0.0
	_interpolator._process(0.0)

	assert_vector(_player.position).is_equal_approx(P1, Vector2(0.5, 0.5))


func test_position_smoothly_interpolates_between_updates() -> void:
	## Core visual-quality test.
	##
	## Given two network snapshots separated by UPDATE_INTERVAL ticks, the
	## displayed position at display_tick == UPDATE_INTERVAL/2 must be close
	## to the midpoint of P0 and P1.
	##
	## With the BACKWARD algorithm (get_latest_at_or_before), t is always >= 1
	## so position always snaps to the newest snapshot — this test catches that.
	##
	## With FORWARD interpolation (prev + next bracketing display_tick):
	## elapsed = (display_tick - prev_tick) + factor = (4 - 0) + 0 = 4
	## span    = next_tick - prev_tick             = 8 - 0     = 8
	## t = 0.5 → lerp(P0, P1, 0.5) = (50, 0) ✓

	const UPDATE_INTERVAL := 8
	_clock.display_offset = UPDATE_INTERVAL  # next snapshot must be in buffer at display time

	_network_update(P0)
	_tick()  # tick 0: P0 recorded

	for _i in UPDATE_INTERVAL - 1:
		_tick()  # ticks 1-7: no change, not re-recorded

	_network_update(P1)
	_tick()  # tick 8: P1 recorded; clock.tick → 9

	# display_tick = clock.tick - display_offset = 9 - 8 = 1.
	# Advance until display_tick = UPDATE_INTERVAL / 2 = 4.
	# We need clock.tick = 4 + UPDATE_INTERVAL = 12.
	for _i in 3:
		_tick()  # clock.tick → 10, 11, 12

	_clock.tick_factor = 0.0
	_interpolator._process(0.0)

	var midpoint := P0.lerp(P1, 0.5)
	assert_vector(_player.position).is_equal_approx(midpoint, Vector2(10.0, 10.0))


func test_tick_factor_produces_sub_tick_movement() -> void:
	## tick_factor should produce visible movement within a single tick interval.
	## With forward interpolation, factor advances t inside [prev_tick, next_tick].
	##
	## At factor=0.0: t = UPDATE_INTERVAL/2 / UPDATE_INTERVAL = 0.5
	## At factor=0.5: t = (UPDATE_INTERVAL/2 + 0.5) / UPDATE_INTERVAL = 0.5625
	## The position at factor=0.5 must be strictly greater than at factor=0.0.

	const UPDATE_INTERVAL := 8
	_clock.display_offset = UPDATE_INTERVAL

	_network_update(P0)
	_tick()  # tick 0
	for _i in UPDATE_INTERVAL - 1:
		_tick()

	_network_update(P1)
	_tick()  # tick 8

	for _i in 3:
		_tick()  # drive to clock.tick=12, display_tick=4

	_clock.tick_factor = 0.0
	_interpolator._process(0.0)
	var pos_at_factor_0 := _player.position.x

	_clock.tick_factor = 0.5
	_interpolator._process(0.0)
	var pos_at_factor_half := _player.position.x

	assert_that(pos_at_factor_half > pos_at_factor_0).is_true()
