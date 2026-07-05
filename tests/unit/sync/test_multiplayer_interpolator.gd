## Unit tests for [MultiplayerInterpolator].
##
## Timing is driven manually. No real physics loop or network stack is needed.
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
	_tree = MultiplayerTree.new()
	add_child(_tree)
	auto_free(_tree)

	_clock = MultiplayerClock.new()
	_clock.tickrate = 30
	_clock.display_offset = 0
	_tree.add_child(_clock)
	auto_free(_clock)
	_clock.set_physics_process(false)

	var api := _clock.multiplayer as SceneMultiplayer
	assert(api != null, "test requires SceneMultiplayer")
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_multiplayer_clock", _clock)

	_player = Node2D.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(999)
	auto_free(_player)

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

	_interpolator = MultiplayerInterpolator.new()
	_interpolator.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	_interpolator.enable_smart_dilation = false
	_interpolator.trace_interval = 1
	_player.add_child(_interpolator)
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


func test_authority_player_position_is_not_modified() -> void:
	_player.set_multiplayer_authority(_player.multiplayer.get_unique_id())
	_network_update(P0)
	_tick()
	_network_update(P1)
	_tick()
	_player.position = P1
	_interp()
	assert_vector(_player.position).is_equal(P1)


func test_remote_interpolation_contract() -> void:
	_player.position = P0
	_interpolator.reset()
	_interp()
	assert_vector(_player.position).is_equal(P0)

	_network_update(P0)
	_tick()
	_tick()
	for _i in 5:
		_interp()
	_network_update(P1)
	_tick()
	var buf: NetwRingBuffer = _interpolator.get_buffer(&"position")
	assert_that(buf.get_at(2)).is_equal(P1)

	_reset_for_display(8)
	_drive_update_at_tick(8)
	for _i in 7:
		_tick()
	_clock.tick_factor = 0.0
	_interp()
	assert_vector(_player.position).is_equal_approx(P1, Vector2(0.5, 0.5))

	_reset_for_display(2)
	_drive_update_at_tick(2)
	_clock.tick_factor = 0.0
	_interp()
	var midpoint := P0.lerp(P1, 0.5)
	assert_vector(_player.position).is_equal_approx(midpoint, Vector2(0.1, 0.1))

	var pos_at_factor_0 := _player.position.x
	_clock.tick_factor = 0.5
	_interp()
	assert_that(_player.position.x > pos_at_factor_0).is_true()


func test_smart_dilation_contract() -> void:
	_interpolator.enable_smart_dilation = true
	_interpolator.display_lag = 99.0

	_interpolator.reset()

	var expected := _expected_min_lag()
	assert_that(_interpolator.display_lag).is_equal_approx(expected, 0.001)

	_network_update(P0)
	_tick()
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

	_interpolator.max_extra_dilation = 10.0
	_interpolator.get_buffer(&"position").clear()
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
	var state := MultiplayerInterpolator._PropertyState.new()
	state.mode = MultiplayerInterpolator.Mode.SLERP

	var a := Quaternion(Vector3.UP, 0.0)
	var b := Quaternion(Vector3.UP, PI / 2.0)
	var mid: Quaternion = state._interpolate(a, b, 0.5)

	assert_that(mid.is_equal_approx(a.slerp(b, 0.5))).is_true()
	assert_that(absf(mid.length() - 1.0) < 0.0001).is_true()


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


func _tick() -> void:
	_clock._physics_process(_clock.ticktime)


func _network_update(pos: Vector2) -> void:
	_player.position = pos
	_interp()


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


func _reset_for_display(display_offset: int) -> void:
	_clock.tick = 0
	_clock.tick_factor = 0.0
	_clock.display_offset = display_offset
	_interpolator.reset()
	_player.position = P0


func _drive_update_at_tick(update_tick: int) -> void:
	_network_update(P0)
	_tick()
	for _i in update_tick - 1:
		_tick()
	_network_update(P1)
	_tick()


func _request_ready_recursive(node: Node) -> void:
	node.request_ready()
	for child in node.get_children():
		_request_ready_recursive(child)
