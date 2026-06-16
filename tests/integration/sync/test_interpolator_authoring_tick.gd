## Integration test for [MultiplayerInterpolator] authoring-tick keying (Decision 8).
##
## When a single stamped [StateSynchronizer] drives an entity, the interpolator
## keys received history by the packet's authoring [constant StampedSynchronizer.TICK]
## instead of the receive tick. Under a delay the two differ, so a shooter can name
## the server tick it actually displayed. This is the rewind-accuracy prerequisite.
class_name TestInterpolatorAuthoringTick
extends NetwTestSuite

var rig: SyncLoopbackRig


func _pos(t: int) -> Vector2:
	return Vector2(t, -t)


func _make_state_sync() -> StampedSynchronizer:
	var sync := StateSynchronizer.new()
	sync.register_property(
		&"position",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		false,
		true,
	)
	return sync


func test_keys_history_by_authoring_tick() -> void:
	rig = SyncLoopbackRig.new()
	await rig.setup(self, _make_state_sync)

	var interp := MultiplayerInterpolator.new()
	interp.name = "Interp"
	interp.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	rig.client_node.add_child(interp)
	interp.owner = rig.client_node
	await (Engine.get_main_loop() as SceneTree).process_frame

	var server_sync := rig.server_sync as StateSynchronizer
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_sync.authored_tick = t
			rig.server_node.position = _pos(t),
	)

	# A clean delay so the authoring tick lags the receive tick.
	rig.delay_server_to_client(5)
	rig.sync_ticks(60)

	var buf := interp.get_buffer(&"position")
	assert_bool(buf != null).is_true()
	var newest := buf.newest_tick()
	assert_int(newest).is_greater(0)

	# The value at its own key is the authored value. Receive-tick keying would
	# instead store the value authored ~delay ticks earlier, so this only holds
	# under authoring-tick keying.
	assert_vector(buf.get_at(newest)).is_equal(_pos(newest))


func test_displayed_authoring_tick_names_a_past_shown_tick() -> void:
	rig = SyncLoopbackRig.new()
	await rig.setup(self, _make_state_sync)

	var interp := MultiplayerInterpolator.new()
	interp.name = "Interp"
	interp.property_modes = { &"position": MultiplayerInterpolator.Mode.LERP }
	rig.client_node.add_child(interp)
	interp.owner = rig.client_node
	await (Engine.get_main_loop() as SceneTree).process_frame

	var server_sync := rig.server_sync as StateSynchronizer
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_sync.authored_tick = t
			rig.server_node.position = _pos(t),
	)

	rig.delay_server_to_client(5)
	rig.sync_ticks(60)
	# Advance the interpolation playhead so a displayed tick exists to name.
	await (Engine.get_main_loop() as SceneTree).process_frame

	var view := interp.displayed_authoring_tick()
	# A server authoring tick is named, it is a real recorded key, and it trails the
	# live server tick (the interpolation playhead is half a round trip behind).
	assert_int(view).is_greater_equal(0)
	assert_vector(interp.get_buffer(&"position").get_at(view)).is_equal(_pos(view))
	assert_int(view).is_less(rig.server_clock.tick)
