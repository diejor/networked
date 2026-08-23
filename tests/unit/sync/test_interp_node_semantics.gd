## Node-truth semantics [method NetwMultiplayerCore.display_pump] and
## [method NetwMultiplayerCore.display_pump_runtime] cannot see.
##
## The pure display math (smoothness, lag, dilation, determinism) is proven by
## the Tier 0 calculus in [code]tests/unit/interpolation/[/code] against the
## engine kernel with no scene. This Tier 1 suite keeps only the truths that need
## real nodes: global-space writes on a parented visual, a moving parent, channel
## inheritance from the body, rigid-body freeze on the remote role, RPC and signal
## argument interpolation, quaternion output, snap and reset against the visual,
## the local-sampler bracketed feed, and the predicted-boundary history
## handoff. It stays deliberately small.
class_name TestInterpNodeSemantics
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
var _clock: NetwClockHandle
var _replication: ReplicationCore
var _native_core: NetwMultiplayerCore
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
	_clock = api._native_core.clock_handle

	_replication = _tree.api._replication
	_native_core = _tree.api._native_core


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
			NetwDisplayHandle.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_smooth_time = 0.05
	_bind_route()

	_player.position = P1
	_native_core.display_pump(1.0 / 60.0)

	assert_float(_visual.global_position.x).is_greater(0.0)
	assert_float(_visual.global_position.x).is_less(P1.x)
	assert_vector(_player.position).is_equal(P1)


func test_display_role_switches_glide_between_remote_and_predicted() -> void:
	_spawn_predicted()
	_entity.prediction.teleport_threshold = 250.0
	_entity.interpolation.chase_glide_time = 0.15
	_player.position = P0
	for frame in 30:
		_render()

	_player.position = P1
	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.REMOTE
	)
	_record(_player, &"position", P1, 1)
	_display_at(1, 0, 0.0)
	assert_float(_visual.global_position.distance_to(P0)) \
			.override_failure_message(
				"the first remote target must retain the prior predicted display",
			).is_less(0.1)
	for frame in 90:
		_render()
	assert_vector(_visual.global_position).is_equal_approx(
		P1,
		Vector2(0.1, 0.1),
	)

	_player.position = P0
	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.PREDICTED
	)
	_render()
	assert_float(_visual.global_position.distance_to(P1)) \
			.override_failure_message(
				"the predicted chase must adopt the prior remote display",
			).is_less(20.0)
	for frame in 90:
		_render()
	assert_vector(_visual.global_position).is_equal_approx(
		P0,
		Vector2(0.1, 0.1),
	)

	# A disabled role writes nothing, so it must not be counted as a pass. A
	# counter that advanced anyway would read identically to a healthy pump,
	# and a frozen display would look like one that ran and chose this value.
	var display: NetwDisplayHandle = _entity.interpolation
	var pumped_predicted := display.pumped_frames
	for frame in 10:
		_render()
	assert_int(_entity.interpolation.pumped_frames).override_failure_message(
		"a running pump must count its passes",
	).is_greater(pumped_predicted)

	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.DISABLED
	)
	_render()
	var disabled_display: NetwDisplayHandle = _entity.interpolation
	var pumped_disabled := disabled_display.pumped_frames
	for frame in 10:
		_render()
	assert_int(_entity.interpolation.pumped_frames).override_failure_message(
		"a pump that declined every pass must not report having run",
	).is_equal(pumped_disabled)


func test_demote_flip_resumes_from_rows_recorded_while_predicted() -> void:
	_spawn_predicted()
	_entity.prediction.teleport_threshold = 250.0
	_player.position = P0
	for frame in 30:
		_render()

	# Authority rows keep arriving during prediction. The chase display must
	# ignore them, but they must land in history so a demote hands the remote
	# pump a warm ring even when no row arrives after the flip.
	_record(_player, &"position", P1, 1)
	_record(_player, &"position", P2, 2)
	var runtime := _native_core.display_book.runtime_of(_entity.rid)
	assert_bool(runtime.states[0].history.is_empty()) \
			.override_failure_message(
				"network rows must record into history during prediction",
			).is_false()
	_render()
	assert_float(_visual.global_position.distance_to(P0)) \
			.override_failure_message(
				"the chase display must keep ignoring network rows",
			).is_less(0.1)

	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.REMOTE
	)
	_display_at(2, 0, 0.0)
	assert_float(_visual.global_position.distance_to(P0)) \
			.override_failure_message(
				"the first remote frame must retain the prior predicted display",
			).is_less(0.1)
	for frame in 90:
		_render()
	assert_vector(_visual.global_position).is_equal_approx(
		P2,
		Vector2(0.1, 0.1),
	)


func test_demote_flip_seeds_role_offset_on_first_frame() -> void:
	_spawn_predicted()
	_player.position = P1
	for frame in 30:
		_render()

	_record(_player, &"position", P2, 1)
	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.REMOTE
	)
	var runtime := _native_core.display_book.runtime_of(_entity.rid)
	var track: StringName = runtime.states[0].name
	assert_bool(runtime.track_stat(track, &"offset_armed")).is_true()

	_display_at(1, 0, 0.0)
	assert_bool(runtime.track_stat(track, &"offset_armed")) \
			.override_failure_message(
				"the role offset must seed on the first frame after a demote",
			).is_false()
	assert_bool(runtime.track_stat(track, &"offset_held")).is_true()


func test_promote_flip_still_clears_history() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_record(_player, &"position", P0, 0)
	_record(_player, &"position", P1, 1)
	_display_at(1, 1, 0.5)

	_entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.PREDICTED
	)
	var runtime := _native_core.display_book.runtime_of(_entity.rid)
	for state in runtime.states:
		assert_bool(state.history.is_empty()) \
				.override_failure_message(
					"a promote to predicted must clear the remote tick domain",
				).is_true()


func test_remote_rigidbody_freezes_and_restores_from_handle_role() -> void:
	var body := RigidBody2D.new()
	body.name = "RemoteBody"
	body.freeze = false
	body.freeze_mode = RigidBody2D.FREEZE_MODE_STATIC
	body.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
	auto_free(body)

	var entity := NetwEntity.ensure(body)
	var spec := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(body, &"position").interpolate(spec)
	_tree.add_child(body)
	_native_core.liveness_bind_route(44, entity)

	assert_bool(body.freeze).is_true()
	assert_int(body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_KINEMATIC)

	entity.interpolation.display_role = (
			NetwDisplayHandle.DisplayRole.DISABLED
	)
	_native_core.display_book.mark_dirty(entity.rid, NetwDisplayDecl.DIRT_RUNTIME)
	await get_tree().process_frame

	assert_bool(body.freeze).is_false()
	assert_int(body.freeze_mode).is_equal(RigidBody2D.FREEZE_MODE_STATIC)


func test_auto_role_disables_a_display_this_peer_holds_authority_over() -> void:
	# Every other target here pins authority to peer 2, so the AUTO ladder's
	# authority rung is only reached by an entity this peer owns. Nothing
	# streams to it, so there is nothing for a display to smooth.
	var node := Node2D.new()
	node.name = "LocallyOwnedPlayer"
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(51, entity)

	_record(node, &"position", P0, 0)

	assert_int(entity.interpolation.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.DISABLED
	)


func test_auto_role_predicts_a_locally_simulated_entity() -> void:
	# A prediction component makes the entity simulate here, and a predicted
	# input source makes it this peer's to predict. That outranks the
	# authority rung above, which would otherwise disable the same entity.
	var node := Node2D.new()
	node.name = "PredictedPlayer"
	var component := Node.new()
	component.name = "PredictionComponent"
	node.add_child(component)
	component.unique_name_in_owner = true
	component.owner = node
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	entity.prediction.input_source = NetwPredict.InputSource.PREDICTED
	Netw.configure_property(node, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(52, entity)

	_record(node, &"position", P0, 0)

	assert_int(entity.interpolation.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.PREDICTED
	)


func test_auto_role_predicts_what_this_peer_steers() -> void:
	# The other half of the prediction rung: local control reaches it without
	# any declared input source, and it outranks the authority this peer also
	# holds over the same entity.
	var node := Node2D.new()
	node.name = "SteeredPlayer"
	var component := Node.new()
	component.name = "PredictionComponent"
	node.add_child(component)
	component.unique_name_in_owner = true
	component.owner = node
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(53, entity)
	entity.set_controller(1)

	_record(node, &"position", P0, 0)
	_native_core.display_mark_role_dirty(entity.rid)

	assert_bool(entity.is_controlled_locally).is_true()
	assert_int(entity.interpolation.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.PREDICTED
	)


func test_scriptless_node_via_overlay_interpolates() -> void:
	var node := Node2D.new()
	node.name = "OverlayPlayer"
	node.position = P0
	node.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(33, entity)

	_record(node, &"position", P0, 0)
	_record(node, &"position", P1, 1)
	_display_at(1, 1, 0.5)

	assert_vector(node.position).is_equal_approx(
		P0.lerp(P1, 0.5),
		Vector2(0.1, 0.1),
	)


func test_unconfigured_record_is_a_noop() -> void:
	var node := Node2D.new()
	node.name = "BarePlayer"
	node.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(31, entity)

	_record(node, &"position", P1, 0)

	assert_that(_native_core.display_book.runtime_of(entity.rid)).is_null()


func test_two_nodes_interpolating_position_do_not_collide() -> void:
	var player := Node2D.new()
	player.name = "TwoNodePlayer"
	player.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
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
	_native_core.liveness_bind_route(21, entity)

	_record(a, &"position", P0, 0)
	_record(b, &"position", P2, 0)

	var runtime := _native_core.display_book.runtime_of(entity.rid)
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
	node.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
	var entity := NetwEntity.ensure(node)
	entity.interpolation.enable_smart_dilation = false
	Netw.configure_property(node, &"quaternion").interpolate(
		NetwInterpolate.new().slerp().smooth(0.0).to(&"quaternion"),
	)
	_tree.add_child(node)
	auto_free(node)
	_native_core.liveness_bind_route(41, entity)

	var q0 := Quaternion.IDENTITY
	var q1 := Quaternion(Vector3.UP, PI / 2.0)
	_record(node, &"quaternion", q0, 0)
	_record(node, &"quaternion", q1, 1)
	_display_at(1, 1, 0.5)

	assert_bool(node.quaternion.is_equal_approx(q0.slerp(q1, 0.5))).is_true()


func test_runtime_rebuild_preserves_interpolation() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_native_core.display_book.mark_dirty(_entity.rid, NetwDisplayDecl.DIRT_RUNTIME)
	await get_tree().process_frame

	_record(_player, &"position", P0, 0)
	_record(_player, &"position", P1, 1)
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
	var runtime := _native_core.display_book.runtime_of(_entity.rid)

	_clock.tickrate = 15
	assert_float(
		_native_core.display_chase_smooth_time(
			runtime,
			NetwDisplayTiming.capture(_clock, 0.0),
		),
	).is_equal_approx(_clock.ticktime * 0.85, 0.0001)

	_clock.tickrate = 60
	assert_float(
		_native_core.display_chase_smooth_time(
			runtime,
			NetwDisplayTiming.capture(_clock, 0.0),
		),
	).is_equal_approx(_clock.ticktime * 0.85, 0.0001)


func test_visual_smoothed_position_survives_moving_parent() -> void:
	_spawn_target(true)
	Netw.configure_property(_player, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.0).to(&"position"),
	)
	_bind_route()

	_record(_player, &"position", P0, 0)
	_record(_player, &"position", P1, 1)

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

	_record(_player, &"position", P0, 0)
	_record(_player, &"position", P1, 1)

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
			NetwDisplayHandle.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_mode = (
			NetwDisplayHandle.PredictedMode.BRACKETED
	)
	_bind_route()

	# The local after_tick sampler feeds the same resample the remote role uses.
	_player.position = P0
	_native_core.display_on_clock_tick(0.0, 0)
	_player.position = P1
	_native_core.display_on_clock_tick(0.0, 1)

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
			NetwDisplayHandle.DisplayRole.PREDICTED
	)
	_entity.interpolation.predicted_smooth_time = 0.05
	_bind_route()


func _spawn_target(use_visual: bool) -> void:
	_player = InterpTarget.new()
	_player.name = "RemotePlayer"
	_player.set_multiplayer_authority(2) # SMELL(authority-pin): no arm() here, which applies authority in production
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
	_native_core.liveness_bind_route(7, _entity)


# Pumps the entity's own runtime directly, one call per invocation, since
# [method NetwMultiplayerCore.display_pump] dedups by engine process frame
# and this helper is called several times within one frame.
func _render(delta: float = 1.0 / 60.0) -> void:
	var runtime := _native_core.display_book.runtime_of(_entity.rid)
	var timing := NetwDisplayTiming.capture(_clock, delta)
	_native_core.display_pump_runtime(
		runtime,
		timing,
		_native_core.display_book.stats,
	)


func _display_at(tick: int, display_offset: int, factor: float) -> void:
	_clock.tick = tick
	_clock.display_offset = display_offset
	_clock.tick_factor_override = factor
	_native_core.display_pump(0.0)


func _record(
		node: Node,
		target_property: StringName,
		value: Variant,
		tick: int,
) -> void:
	var spec := NetwScriptModel.get_node_property_interpolator(
		node,
		target_property,
	)
	_native_core.display_record(node, target_property, value, tick, spec, false)


func _dispatch_property(property: StringName, value: Variant) -> void:
	var w := NetwBitBufferWriter.new()
	NetwScriptModel.write_token(w, property)
	NetwCodec.write_values(w, [value], [null], [typeof(value)])
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
	var w := NetwBitBufferWriter.new()
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
	var w := NetwBitBufferWriter.new()
	NetwScriptModel.write_token(w, signal_name)
	NetwCodec.write_values(
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
