## Unit tests for [NetwInterpolationInterface] and the authoring shell.
class_name TestMultiplayerInterpolator
extends NetwTestSuite

const P0 := Vector2(0.0, 0.0)
const P1 := Vector2(100.0, 0.0)
const P2 := Vector2(200.0, 0.0)


class InterpTarget:
	extends Node2D

	signal nudged(offset: Vector2)

	var rpc_target := Vector2.ZERO
	var signal_target := Vector2.ZERO
	var last_rpc_arg := Vector2.ZERO


	@rpc("any_peer", "call_remote", "reliable") func apply_rpc(offset: Vector2) -> void:
		last_rpc_arg = offset


var _tree: MultiplayerTree
var _clock_node: MultiplayerClock
var _clock: NetwClockInterface
var _replication: NetwReplicationInterface
var _liveness: NetwLivenessInterface
var _iface: NetwInterpolationInterface
var _player: InterpTarget
var _visual: Node2D
var _entity: NetwEntity


func before_test() -> void:
	_tree = MultiplayerTree.new()
	_tree.name = "InterpolationTree"

	_clock_node = MultiplayerClock.new()
	_clock_node.tickrate = 30
	_clock_node.display_offset = 0
	_clock_node.set_physics_process(false)
	_tree.add_child(_clock_node)

	add_child(_tree)
	auto_free(_tree)

	var api := _clock_node.multiplayer as NetwMultiplayer
	assert(api != null, "test requires NetwMultiplayer")
	api.set_meta(&"_multiplayer_tree", _tree)
	api.set_meta(&"_multiplayer_clock", _clock_node)
	_clock = api.clock

	_replication = _tree.api.replication
	_liveness = _tree.api.liveness
	_iface = _tree.get_service(NetwInterpolationInterface) \
			as NetwInterpolationInterface


func after_test() -> void:
	var api := _clock_node.multiplayer as NetwMultiplayer
	if api:
		if api.has_meta(&"_multiplayer_clock"):
			api.remove_meta(&"_multiplayer_clock")
		if api.has_meta(&"_multiplayer_tree"):
			api.remove_meta(&"_multiplayer_tree")
	await super.after_test()


func test_property_sync_interpolates_without_synchronizer() -> void:
	_spawn_target(true)
	var spec := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(_player, &"position").interpolate(spec)
	_bind_route()

	_clock.tick = 0
	_dispatch_property(&"position", P0)
	_clock.tick = 1
	_dispatch_property(&"position", P1)

	_display_at(1, 1, 0.5)

	assert_vector(_player.position).is_equal(P1)
	assert_vector(_visual.global_position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func test_rpc_arg_drives_interpolated_target() -> void:
	_spawn_target(false)
	Netw.configure_rpc(_player.apply_rpc).interpolate(
		[NetwInterpolate.new().lerp().smooth(0.0).to(&"rpc_target")],
	)
	_bind_route()

	_clock.tick = 4
	_dispatch_rpc(&"apply_rpc", [P1])
	_display_at(4, 0, 0.0)

	assert_vector(_player.last_rpc_arg).is_equal(P1)
	assert_vector(_player.rpc_target).is_equal(P1)


func test_signal_arg_drives_interpolated_target() -> void:
	_spawn_target(false)
	Netw.configure_signal(_player.nudged).interpolate(
		[NetwInterpolate.new().lerp().smooth(0.0).to(&"signal_target")],
	)
	var received: Array[Vector2] = []
	_player.nudged.connect(
		func(offset: Vector2) -> void:
			received.append(offset)
	)
	_bind_route()

	_clock.tick = 8
	_dispatch_signal(&"nudged", [P2])
	_display_at(8, 0, 0.0)

	assert_array(received).contains_exactly([P2])
	assert_vector(_player.signal_target).is_equal(P2)


func test_predicted_chase_moves_visual_toward_live_source() -> void:
	_spawn_target(true)
	var spec := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(_player, &"position").interpolate(spec)
	_entity.interpolation.display_role = (
			NetwInterpolationInterface.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_smooth_time = 0.05
	_bind_route()

	_player.position = P1
	_iface._process(1.0 / 60.0)

	assert_float(_visual.global_position.x).is_greater(0.0)
	assert_float(_visual.global_position.x).is_less(P1.x)
	assert_vector(_player.position).is_equal(P1)


func test_remote_rigidbody_freezes_and_restores_from_handle_role() -> void:
	var body := RigidBody2D.new()
	body.name = "RemoteBody"
	body.freeze = false
	body.freeze_mode = RigidBody2D.FREEZE_MODE_STATIC
	body.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	auto_free(body)

	var entity := NetwEntity.ensure(body)
	var spec := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(body, &"position").interpolate(spec)
	_tree.add_child(body)
	_liveness.bind_route(44, entity)

	assert_bool(body.freeze).is_true()
	assert_int(body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_KINEMATIC)

	entity.interpolation.display_role = (
			NetwInterpolationInterface.DisplayRole.DISABLED
	)
	_iface._mark_runtime_dirty(entity.interpolation)
	await get_tree().process_frame

	assert_bool(body.freeze).is_false()
	assert_int(body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_STATIC)


func test_scriptless_node_via_overlay_interpolates() -> void:
	var node := Node2D.new()
	node.name = "OverlayPlayer"
	node.position = P0
	node.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(node)
	auto_free(node)
	_liveness.bind_route(33, entity)

	_iface.record(node, &"position", P0, 0)
	_iface.record(node, &"position", P1, 1)
	_display_at(1, 1, 0.5)

	assert_vector(node.position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func test_unconfigured_record_is_a_noop() -> void:
	var node := Node2D.new()
	node.name = "BarePlayer"
	node.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	_tree.add_child(node)
	auto_free(node)
	_liveness.bind_route(31, entity)

	_iface.record(node, &"position", P1, 0)

	assert_that(_iface._runtime_for_handle(entity.interpolation)).is_null()


func test_two_nodes_interpolating_position_do_not_collide() -> void:
	var player := Node2D.new()
	player.name = "TwoNodePlayer"
	player.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	var a := Node2D.new()
	a.name = "A"
	player.add_child(a)
	var b := Node2D.new()
	b.name = "B"
	player.add_child(b)
	var entity := NetwEntity.ensure(player)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(a, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	Netw.configure_property(b, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(player)
	auto_free(player)
	_liveness.bind_route(21, entity)

	_iface.record(a, &"position", P0, 0)
	_iface.record(b, &"position", P2, 0)

	var runtime := _iface._runtime_for_handle(entity.interpolation)
	assert_that(runtime).is_not_null()
	var sources: Array = []
	for state in runtime.states:
		if state.name == &"position":
			sources.append(state.source_obj)
	assert_int(sources.size()).is_equal(2)
	assert_bool(a in sources).is_true()
	assert_bool(b in sources).is_true()


func test_slerp_interpolates_quaternion_rotation() -> void:
	var node := Node3D.new()
	node.name = "RotPlayer"
	node.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"quaternion").interpolate(
		NetwInterpolate.new().slerp().smooth(0.0).to(&"quaternion"),
	)
	_tree.add_child(node)
	auto_free(node)
	_liveness.bind_route(41, entity)

	var q0 := Quaternion.IDENTITY
	var q1 := Quaternion(Vector3.UP, PI / 2.0)
	_iface.record(node, &"quaternion", q0, 0)
	_iface.record(node, &"quaternion", q1, 1)
	_display_at(1, 1, 0.5)

	assert_bool(node.quaternion.is_equal_approx(q0.slerp(q1, 0.5))).is_true()


func test_smart_dilation_grows_display_lag_when_starved() -> void:
	_spawn_target(false)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().to(&"position"),
	)
	_entity.interpolation.enable_smart_dilation = true
	_bind_route()

	_clock.tick = 0
	_dispatch_property(&"position", P0)
	_clock.tick = 1
	_dispatch_property(&"position", P1)

	for _i in 8:
		_display_at(30, 0, 0.0)

	assert_float(_entity.interpolation.display_lag).is_greater(0.0)
	assert_int(_entity.interpolation.starvation_ticks).is_greater(0)


func test_runtime_rebuild_preserves_interpolation() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_iface._mark_runtime_dirty(_entity.interpolation)
	await get_tree().process_frame

	_iface.record(_player, &"position", P0, 0)
	_iface.record(_player, &"position", P1, 1)
	_display_at(1, 1, 0.5)

	assert_vector(_visual.global_position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func test_predicted_reset_snaps_visual_to_live_source() -> void:
	_spawn_predicted()
	_player.position = P1
	_render()
	assert_float(_visual.global_position.x).is_less(P1.x)

	_entity.interpolation.reset()

	assert_vector(_visual.global_position).is_equal_approx(P1, Vector2(0.1, 0.1))


func test_predicted_snap_property_updates_visual_without_history() -> void:
	_spawn_predicted()
	_entity.interpolation.snap_property(&"position", P2)

	assert_vector(_visual.global_position).is_equal_approx(P2, Vector2(0.1, 0.1))
	assert_that(_entity.interpolation.get_buffer(&"position")).is_null()


func test_predicted_correction_snap_is_absorbed_continuously() -> void:
	_spawn_predicted()
	_player.position = P1
	_render()
	var before := _visual.global_position.x

	_player.position = P2
	_render()
	var after := _visual.global_position.x

	assert_float(after).is_greater(before)
	assert_float(after).is_less(P2.x)

	for _i in 12:
		_render()

	assert_float(_visual.global_position.x).is_greater(after)
	assert_float(_visual.global_position.x).is_less_equal(P2.x)


func test_predicted_auto_smooth_time_tracks_clock_ticktime() -> void:
	_spawn_predicted()
	_entity.interpolation.predicted_smooth_time = 0.0
	var runtime := _iface._runtime_for_handle(_entity.interpolation)

	_clock.tickrate = 15
	assert_float(
		_iface._predicted_effective_smooth_time(runtime),
	).is_equal_approx(_clock.ticktime * 0.85, 0.0001)

	_clock.tickrate = 60
	assert_float(
		_iface._predicted_effective_smooth_time(runtime),
	).is_equal_approx(_clock.ticktime * 0.85, 0.0001)


func test_visual_smoothed_position_survives_moving_parent() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_iface.record(_player, &"position", P0, 0)
	_iface.record(_player, &"position", P1, 1)

	# Moving the body must not drag the smoothed channel, since it is written
	# in global space every frame.
	_player.position = Vector2(500.0, 500.0)
	_display_at(1, 1, 0.5)

	assert_vector(_visual.global_position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func test_non_interpolated_channel_inherits_from_body() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_iface.record(_player, &"position", P0, 0)
	_iface.record(_player, &"position", P1, 1)

	# The visual stays parented, so an unsmoothed channel still inherits from
	# the body instead of being severed by top_level.
	_player.scale = Vector2(2.0, 2.0)
	_display_at(1, 1, 0.5)

	assert_vector(_visual.global_scale).is_equal_approx(
		Vector2(2.0, 2.0),
		Vector2(0.01, 0.01),
	)


func test_bracketed_predicted_equals_remote_pipeline_fed_locally() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_entity.interpolation.display_role = (
			NetwInterpolationInterface.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_mode = (
			NetwInterpolationInterface.PredictedMode.BRACKETED
	)
	_bind_route()

	# The local after_tick sampler feeds the same resample the remote role uses.
	_player.position = P0
	_iface._on_clock_tick(0.0, 0)
	_player.position = P1
	_iface._on_clock_tick(0.0, 1)

	_display_at(1, 0, 0.5)

	assert_vector(_visual.global_position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func _spawn_predicted() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_entity.interpolation.display_role = (
			NetwInterpolationInterface.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_smooth_time = 0.05
	_bind_route()


func _spawn_target(use_visual: bool) -> void:
	_player = InterpTarget.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(2)  # SMELL(authority-pin): no arm() here, which applies authority in production
	_entity = NetwEntity.ensure(_player)
	_entity.interpolation.enable_smart_dilation = false
	if use_visual:
		_visual = Node2D.new()
		_visual.name = "Visual"
		_player.add_child(_visual)
		_entity.interpolation.visual_root = NodePath("Visual")
	else:
		_visual = null
	_tree.add_child(_player)
	auto_free(_player)


func _bind_route() -> void:
	_liveness.bind_route(7, _entity)


func _render(delta: float = 1.0 / 60.0) -> void:
	_iface._last_update_frame = -1
	_iface._process(delta)


func _display_at(tick: int, display_offset: int, factor: float) -> void:
	_clock.tick = tick
	_clock.display_offset = display_offset
	_clock.tick_factor = factor
	_iface._process(0.0)


func _dispatch_property(property: StringName, value: Variant) -> void:
	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_token(w, property)
	NetwScriptModel.write_values(w, [value], [null], [typeof(value)])
	_replication._dispatch(
		_entity.route,
		0,
		NetwFrameEnvelope.Channel.PROPERTY_SYNC,
		w.to_bytes(),
		"",
		1,
		true,
	)


func _dispatch_rpc(method: StringName, args: Array) -> void:
	var w := NetwBitBuffer.Writer.new()
	w.put_aligned_u8(0)
	NetwScriptModel.write_call_body(
		w,
		method,
		args,
		[],
		NetwScriptModel.get_method_arg_types(_player.get_script(), method),
	)
	_replication._dispatch(
		_entity.route,
		0,
		NetwFrameEnvelope.Channel.CALL,
		w.to_bytes(),
		"",
		1,
		true,
	)


func _dispatch_signal(signal_name: StringName, args: Array) -> void:
	var w := NetwBitBuffer.Writer.new()
	NetwScriptModel.write_token(w, signal_name)
	NetwScriptModel.write_values(
		w,
		args,
		[],
		NetwScriptModel.get_signal_arg_types(_player.get_script(), signal_name),
	)
	_replication._dispatch(
		_entity.route,
		0,
		NetwFrameEnvelope.Channel.SIGNAL,
		w.to_bytes(),
		"",
		1,
		true,
	)
