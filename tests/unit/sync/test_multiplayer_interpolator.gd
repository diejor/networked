## Unit tests for [MultiplayerInterpolator].
##
## All timing is driven manually - no real physics loop or network stack needed.
## The test controls exactly which ticks fire and when process frames run,
## making assertions on [member Node2D.position] deterministic.
class_name TestMultiplayerInterpolator
extends NetwTestSuite

const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)

var _player: Node2D
var _clock: MultiplayerClock
var _tree: MultiplayerTree
var _interpolator: MultiplayerInterpolator
var _sync: MultiplayerSynchronizer


func before_test() -> void:
	# Tree - required for NetwComponent bucket lookups.
	_tree = MultiplayerTree.new()
	add_child(_tree)
	auto_free(_tree)

	# Clock - manually driven, not connected to the physics loop.
	_clock = MultiplayerClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0 # display_tick = clock.tick; easier reasoning
	_tree.add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)

	var api := _clock.multiplayer as SceneMultiplayer
	assert(api != null, "test requires SceneMultiplayer")
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_multiplayer_clock", _clock)

	# Remote player - authority 999 ≠ local peer 1.
	_player = Node2D.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(999)
	auto_free(_player)

	# Synchronizer - required to avoid tracker warnings.
	_sync = MultiplayerSynchronizer.new()
	_sync.name = "MultiplayerSynchronizer"
	var cfg := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(
		ppath,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)
	_sync.replication_config = cfg
	_sync.set_multiplayer_authority(999)
	_player.add_child(_sync)

	# Interpolator
	_interpolator = MultiplayerInterpolator.new()
	_interpolator.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	_interpolator.enable_smart_dilation = false
	_interpolator.trace_interval = 1
	_player.add_child(_interpolator)
	# Set owners BEFORE entering tree for discovery
	_sync.owner = _player
	_interpolator.owner = _player

	_tree.add_child(_player)

	await get_tree().process_frame


func after_test() -> void:
	var api := _clock.multiplayer as SceneMultiplayer
	if api:
		if api.has_meta(&"_multiplayer_clock"):
			api.remove_meta(&"_multiplayer_clock")
		if api.has_meta(&"_multiplayer_tree"):
			api.remove_meta(&"_multiplayer_tree")
	await super.after_test()


## Advances the clock by one tick, which fires [signal MultiplayerClock.after_tick]
## and therefore [method MultiplayerInterpolator._record_tick].
func _tick() -> void:
	_clock._physics_process(_clock.ticktime)


## Simulates a network packet: sets [member Node2D.position] then runs a process
## frame so the snapshot is captured before the next tick.
func _network_update(pos: Vector2) -> void:
	_player.position = pos
	_interp()


## Triggers a visual interpolation frame manually.
func _interp() -> void:
	_interpolator._update_instance(
		_clock.display_tick,
		_clock.tick_factor,
		0.0,
		1.0,
	)


func _expected_min_lag() -> float:
	var needed := float(_interpolator._expected_interval_ticks + 1)
	var network_padding := float(
		maxi(0, _clock.recommended_display_offset - _clock.display_offset),
	)
	return maxf(0.0, needed - float(_clock.display_offset) + network_padding)


func test_authority_player_position_is_not_modified_by_process() -> void:
	# _process must be a no-op when the owner is the local authority,
	# otherwise the interpolator fights with the player's own _physics_process.
	_player.set_multiplayer_authority(_player.multiplayer.get_unique_id())

	_network_update(P0)
	_tick()
	_network_update(P1)
	_tick()

	_player.position = P1 # authority set this via move_and_slide
	_interp()

	assert_vector(_player.position).is_equal(P1)


func test_empty_buffer_leaves_position_unchanged() -> void:
	# With no ticks fired, the buffer is empty.
	# _process must not touch the position.
	_player.position = P0
	_interp()
	assert_vector(_player.position).is_equal(P0)


func test_buffer_records_network_value_not_interpolated_value() -> void:
	# _process writes a lerped value to owner.position.
	# _record_tick must NOT read that lerped value back into the buffer -
	# it must read the latest network snapshot instead.

	_network_update(P0)
	_tick() # buf records P0 at tick 0; clock.tick -> 1
	_tick() # buf records P0 again (sparse: skipped); clock.tick -> 2

	# Run several render frames - _process writes lerp results to position.
	for _i in 5:
		_interp()

	# Now a network packet arrives with P1.
	_network_update(P1)
	_tick() # buf should record P1 at tick 2, NOT whatever _process last wrote

	var buf: NetwRingBuffer = _interpolator.get_buffer(&"position")
	assert_that(buf.get_at(2)).is_equal(P1)


func test_position_is_at_p0_before_any_update() -> void:
	# Before the second snapshot arrives, the interpolator can only show P0.
	_network_update(P0)
	_tick() # buf: {0: P0}

	_clock.tick_factor = 0.5
	_interp()

	assert_vector(_player.position).is_equal_approx(P0, Vector2(0.1, 0.1))


func test_position_reaches_p1_after_update_tick() -> void:
	# At display_tick = 8 and factor = 1.0, the position must equal P1 exactly.
	const UPDATE_TICK := 8
	_clock.display_offset = UPDATE_TICK # snapshots are in buffer when displayed

	_network_update(P0)
	_tick() # tick 0: P0 recorded

	for _i in UPDATE_TICK - 1:
		_tick() # ticks 1-7: snapshot unchanged

	_network_update(P1)
	_tick() # tick 8: P1 recorded; clock.tick -> 9

	# Drive forward until display_tick = UPDATE_TICK.
	for _i in UPDATE_TICK - 1:
		_tick()

	_clock.tick_factor = 0.0
	_interp()

	assert_vector(_player.position).is_equal_approx(P1, Vector2(0.5, 0.5))


func test_position_smoothly_interpolates_between_updates() -> void:
	# Given two network snapshots separated by UPDATE_INTERVAL ticks, the
	# displayed position at display_tick == UPDATE_INTERVAL/2 must be close
	# to the midpoint of P0 and P1.

	const UPDATE_INTERVAL := 2
	_clock.display_offset = UPDATE_INTERVAL

	_network_update(P0)
	_tick() # tick 0: P0 recorded

	for _i in UPDATE_INTERVAL - 1:
		_tick() # ticks 1: no change

	_network_update(P1)
	_tick() # tick 2: P1 recorded; clock.tick -> 3

	# display_tick = clock.tick - display_offset = 3 - 2 = 1.
	# Midway between tick 0 and tick 2.
	_clock.tick_factor = 0.0
	_interp()

	var midpoint := P0.lerp(P1, 0.5)
	assert_vector(_player.position).is_equal_approx(midpoint, Vector2(0.1, 0.1))


func test_reset_seeds_display_lag_to_min_lag() -> void:
	# reset() must anchor both the resting floor and the live lag to the
	# computed minimum, so dilation does not ramp from zero on spawn.
	_interpolator.enable_smart_dilation = true
	_interpolator.display_lag = 99.0

	_interpolator.reset()

	var expected := _expected_min_lag()
	assert_that(_interpolator.display_lag).is_equal_approx(expected, 0.001)


func test_dilation_eases_display_lag_toward_floor() -> void:
	# With data available (not starving), display_lag eases back down toward the
	# resting floor a fraction at a time instead of snapping.
	_interpolator.enable_smart_dilation = true
	_network_update(P0)
	_tick() # record a snapshot so the buffer is not starving

	_interpolator.reset()
	var target_floor := _interpolator.display_lag
	_interpolator.display_lag = target_floor + 8.0
	_interpolator.starvation_ticks = 0

	_interpolator._update_instance(
		_clock.display_tick,
		_clock.tick_factor,
		0.016,
		1.0,
	)

	assert_that(_interpolator.display_lag < target_floor + 8.0).is_true()
	assert_that(_interpolator.display_lag > target_floor).is_true()


func test_dilation_grows_past_floor_on_sustained_starvation() -> void:
	# An empty buffer starves every frame; once past the grace window the lag
	# must climb above the resting floor to rebuild the buffer.
	_interpolator.enable_smart_dilation = true
	_interpolator.max_extra_dilation = 10.0
	_interpolator.reset()
	var start_lag := _interpolator.display_lag

	for _i in 6:
		_interpolator._update_instance(
			_clock.display_tick,
			_clock.tick_factor,
			0.5,
			1.0,
		)

	assert_that(_interpolator.starvation_ticks >= 6).is_true()
	assert_that(_interpolator.display_lag > start_lag).is_true()


func test_slerp_mode_uses_spherical_interpolation() -> void:
	# SLERP must take the spherical path between quaternions, not a
	# component-wise lerp (which denormalizes and rotates non-uniformly).
	var state := MultiplayerInterpolator._PropertyState.new()
	state.mode = MultiplayerInterpolator.Mode.SLERP

	var a := Quaternion(Vector3.UP, 0.0)
	var b := Quaternion(Vector3.UP, PI / 2.0)
	var mid: Quaternion = state._interpolate(a, b, 0.5)

	assert_that(mid.is_equal_approx(a.slerp(b, 0.5))).is_true()
	# A true slerp stays unit-length; a raw lerp of these would not.
	assert_that(absf(mid.length() - 1.0) < 0.0001).is_true()


func test_tick_factor_produces_sub_tick_movement() -> void:
	# tick_factor should produce visible movement within a single tick interval.

	const UPDATE_INTERVAL := 2
	_clock.display_offset = UPDATE_INTERVAL

	_network_update(P0)
	_tick() # tick 0
	for _i in UPDATE_INTERVAL - 1:
		_tick()

	_network_update(P1)
	_tick() # tick 2

	# clock.tick is now 3, display_tick = 3 - 2 = 1.
	_clock.tick_factor = 0.0
	_interp()
	var pos_at_factor_0 := _player.position.x

	_clock.tick_factor = 0.5
	_interp()
	var pos_at_factor_half := _player.position.x

	assert_that(pos_at_factor_half > pos_at_factor_0).is_true()


func test_remote_strategy_rebuilds_after_tree_reentry() -> void:
	assert_that(_interpolator._strategy).is_not_null()
	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.REMOTE,
	)

	_tree.remove_child(_player)
	await get_tree().process_frame

	assert_that(_interpolator._strategy).is_null()
	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.DISABLED,
	)

	_request_ready_recursive(_player)
	_tree.add_child(_player)
	await get_tree().process_frame

	assert_that(_interpolator._strategy).is_not_null()
	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.REMOTE,
	)


func _request_ready_recursive(node: Node) -> void:
	node.request_ready()
	for child in node.get_children():
		_request_ready_recursive(child)
