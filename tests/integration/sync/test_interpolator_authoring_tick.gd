## Integration test for [NetwInterpolationInterface] authoring tick keying.
##
## When a single stamped derived state set drives an entity, the service keys
## received history by the frame's authoring tick instead of the receive tick.
## Under a delay the two differ, so a shooter can name the server tick it
## displayed.
class_name TestInterpolatorAuthoringTick
extends NetwTestSuite

var rig: DerivedLoopbackRig


func _pos(t: int) -> Vector2:
	return Vector2(t, -t)


# Configures the position interpolator on the client node and drives the server
# state stream under a clean delay, so the authoring tick lags the receive tick.
func _drive_delayed_stream() -> NetwEntity:
	var entity := NetwEntity.of(rig.client_node)
	var interp := MultiplayerInterpolator.new()
	interp.name = "Interp"
	interp.property_interpolators = {
		&"position": NetwInterpolate.new().lerp().smooth(0.0),
	}
	rig.client_node.add_child(interp)
	interp.owner = rig.client_node
	await (Engine.get_main_loop() as SceneTree).process_frame

	var server_binding := NetwEntity.of(rig.server_node).state_binding
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_binding.authored_tick = t
			rig.server_node.position = _pos(t),
	)

	rig.delay_server_to_client(5)
	rig.sync_ticks(60)
	return entity


func test_keys_history_by_authoring_tick() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)
	var entity := await _drive_delayed_stream()

	var buf := entity.interpolation.get_buffer(&"position")
	assert_bool(buf != null).is_true()
	var newest := buf.newest_tick()
	assert_int(newest).is_greater(0)

	# The value at its own key is the authored value. Receive-tick keying would
	# instead store the value authored ~delay ticks earlier, so this only holds
	# under authoring-tick keying.
	assert_vector(buf.get_at(newest)).is_equal(_pos(newest))


func test_displayed_authoring_tick_names_a_past_shown_tick() -> void:
	rig = DerivedLoopbackRig.new()
	await rig.setup(self)
	var entity := await _drive_delayed_stream()
	# Advance the interpolation playhead so a displayed tick exists to name.
	await (Engine.get_main_loop() as SceneTree).process_frame

	var view := entity.interpolation.displayed_authoring_tick()
	# A server authoring tick is named, it is a real recorded key, and it
	# trails the live server tick.
	assert_int(view).is_greater_equal(0)
	assert_vector(
		entity.interpolation.get_buffer(&"position").get_at(view),
	).is_equal(_pos(view))
	assert_int(view).is_less(rig.server_clock.tick)
