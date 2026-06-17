## Unit tests for [MultiplayerInterpolator] predicted display roles.
class_name TestMultiplayerInterpolatorPredicted
extends NetwTestSuite

const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)
const P2 := Vector2(200.0, 0.0)

var _player: Node2D
var _visual: Node2D
var _clock: MultiplayerClock
var _tree: MultiplayerTree
var _interpolator: MultiplayerInterpolator
var _prediction: PredictionComponent
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
	_player.name = "PredictedPlayer"
	_player.position = P0
	_player.set_multiplayer_authority(1)
	auto_free(_player)

	_visual = Node2D.new()
	_visual.name = "Visual"
	_player.add_child(_visual)

	_prediction = PredictionComponent.new()
	_player.add_child(_prediction)
	_prediction.owner = _player

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
	_sync.set_multiplayer_authority(1)
	_player.add_child(_sync)
	_sync.owner = _player

	_interpolator = MultiplayerInterpolator.new()
	_interpolator.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	_interpolator.visual_root = NodePath("../Visual")
	_interpolator.predicted_smooth_time = 0.05
	_player.add_child(_interpolator)
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


func _interp(delta: float = 1.0 / 60.0) -> void:
	_interpolator._update_instance(
		_clock.display_tick,
		_clock.tick_factor,
		delta * _clock.tickrate,
		1.0,
		delta,
	)


func test_auto_role_uses_predicted_strategy_for_local_prediction() -> void:
	assert_int(_interpolator._strategy_role).is_equal(
		MultiplayerInterpolator.DisplayRole.PREDICTED,
	)


func test_predicted_strategy_does_not_subscribe_to_sync_signals() -> void:
	assert_bool(_sync.synchronized.is_connected(_interpolator._on_synced)).is_false()
	assert_bool(_sync.delta_synchronized.is_connected(_interpolator._on_synced)).is_false()


func test_predicted_public_history_api_is_empty() -> void:
	assert_that(_interpolator.get_buffer(&"position")).is_null()
	assert_int(_interpolator.displayed_authoring_tick()).is_equal(-1)


func test_predicted_reset_snaps_visual_to_live_source() -> void:
	_player.position = P1
	_interp()
	assert_float(_visual.global_position.x).is_less(P1.x)

	_interpolator.reset()

	assert_vector(_visual.global_position).is_equal(P1)


func test_predicted_snap_property_updates_visual_without_history() -> void:
	_interpolator.snap_property(&"position", P2)

	assert_vector(_visual.global_position).is_equal(P2)
	assert_that(_interpolator.get_buffer(&"position")).is_null()


func test_chase_moves_visual_on_first_render_frame_without_snapping() -> void:
	_player.position = P1

	_interp()

	assert_float(_visual.global_position.x).is_greater(0.0)
	assert_float(_visual.global_position.x).is_less(P1.x)
	assert_vector(_player.position).is_equal(P1)


func test_correction_snap_is_absorbed_continuously() -> void:
	_player.position = P1
	_interp()
	var before_correction := _visual.global_position.x

	_player.position = P2
	_interp()
	var after_correction := _visual.global_position.x

	assert_float(after_correction).is_greater(before_correction)
	assert_float(after_correction).is_less(P2.x)

	for _i in 12:
		_interp()

	assert_float(_visual.global_position.x).is_greater(after_correction)
	assert_float(_visual.global_position.x).is_less_equal(P2.x)


func test_auto_smooth_time_tracks_clock_ticktime() -> void:
	_interpolator.predicted_smooth_time = 0.0

	_clock.tickrate = 15
	assert_float(_interpolator._predicted_effective_smooth_time()).is_equal_approx(
		_clock.ticktime * 0.85,
		0.0001,
	)

	_clock.tickrate = 60
	assert_float(_interpolator._predicted_effective_smooth_time()).is_equal_approx(
		_clock.ticktime * 0.85,
		0.0001,
	)
