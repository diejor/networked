## Integration tests for [TickInterpolator] edge cases.
##
## Covers authority handover, snapping, teleportation, and feedback loops.
class_name TestTickInterpolatorEdgeCases
extends NetworkedTestSuite

var _runner: GdUnitSceneRunner
var _harness: TickNetworkTestHarness
var _env: TickSimulationEnvironment


func before_test() -> void:
	_runner = scene_runner("res://tests/helpers/tick_test_stage.tscn")
	_harness = auto_free(TickNetworkTestHarness.new())
	
	add_child(_harness)
	await _harness.setup(_runner)
	_env = await _harness.create_environment(&"EdgeCasePlayer")
	
	# Ensure the clock is synchronized before we start any movement tests
	await _harness.wait_for_clock_sync()
	
	var interpolator: TickInterpolator = _env.client_node.get_node(
		"TickInterpolator")
	interpolator.trace_interval = 0

	_harness.set_time_factor(10.0)


func after_test() -> void:
	if is_instance_valid(_harness):
		await _harness.teardown()
	await drain_frames(get_tree(), 3)


func test_authority_handover() -> void:
	const START := Vector2(0.0, 0.0)
	const CLIENT_MOVE := Vector2(50.0, 50.0)

	_env.set_server_property(&"position", START)
	await _harness.yield_to_sync()
	
	assert_vector(_env.get_client_property(&"position")).is_equal_approx(
		START, Vector2(1, 1))

	var client_peer_id := _harness.get_client().multiplayer.get_unique_id()
	_env.server_node.set_multiplayer_authority(client_peer_id)
	_env.client_node.set_multiplayer_authority(client_peer_id)

	_env.client_node.position = CLIENT_MOVE

	await _harness.sync_ticks(10)

	assert_vector(_env.client_node.position).is_equal_approx(
		CLIENT_MOVE, Vector2(1, 1))


func test_first_frame_snapping() -> void:
	_env.client_node.position = Vector2.ZERO
	const FIRST_POS := Vector2(200.0, 100.0)

	var interpolator: TickInterpolator = _env.client_node.get_node(
		"TickInterpolator")

	interpolator.snap_property(&"position", FIRST_POS)

	_env.set_server_property(&"position", FIRST_POS)
	await _harness.yield_to_sync()

	var actual_pos: Vector2 = _env.client_node.position
	assert_vector(actual_pos).is_equal_approx(FIRST_POS, Vector2(0.01, 0.01))


func test_teleport_prevents_first_frame_lerp() -> void:
	_env.client_node.position = Vector2.ZERO
	const FIRST_POS := Vector2(200.0, 100.0)
	
	var interpolator: TickInterpolator = _env.client_node.get_node(
		"TickInterpolator")

	_env.client_node.position = FIRST_POS
	interpolator.reset()

	_env.set_server_property(&"position", FIRST_POS)
	await _harness.yield_to_sync()

	var actual_pos: Vector2 = _env.client_node.position
	assert_vector(actual_pos).is_equal_approx(FIRST_POS, Vector2(0.01, 0.01))


func test_feedback_loop_guard() -> void:
	const P0 := Vector2(100.0, 0.0)
	const P1 := Vector2(200.0, 0.0)

	_env.set_server_property(&"position", P0)
	await _harness.yield_to_sync()

	await _harness.sync_ticks(20)

	_env.set_server_property(&"position", P1)
	await _harness.sync_ticks(40)

	var newest_tick: int = _env.get_buffer_newest_tick(&"position")
	assert_int(newest_tick).is_not_equal(-1)
	
	var snap: Variant = _env.get_buffer_at(&"position", newest_tick)
	assert_vector(snap).is_equal_approx(P1, Vector2(0.01, 0.01))


func test_visual_smooth_movement_realtime() -> void:
	var start_pos := Vector2(100, 300)
	var step_size := 40.0
	var iterations := 10
	
	_env.set_server_property(&"position", start_pos)
	await _harness.yield_to_sync()
	
	var expected_x := start_pos.x
	for i in range(1, iterations + 1):
		expected_x += step_size
		_env.set_server_property(&"position", Vector2(expected_x, 300))
		await _harness.sync_ticks(5)

	await _harness.sync_ticks(30)
	
	var final_client_pos = _env.get_client_property(&"position")
	assert_vector(final_client_pos).is_equal_approx(
		Vector2(expected_x, 300), Vector2(5, 5))


func test_visual_smooth_movement_dynamic_path() -> void:
	var waypoints: Array[Vector2] = [
		Vector2(100, 300),
		Vector2(250, 150),
		Vector2(700, 250),
		Vector2(750, 450),
		Vector2(600, 600),
		Vector2(1100, 100)
	]

	_env.set_server_property(&"position", waypoints[0])
	await _harness.yield_to_sync()
	
	for i in range(1, waypoints.size()):
		_env.set_server_property(&"position", waypoints[i])
		
		await _harness.sync_ticks(6)

	
	await _harness.sync_ticks(20)
	
	var final_client_pos = _env.get_client_property(&"position")
	assert_vector(final_client_pos).is_equal_approx(
		waypoints.back(), Vector2(5, 5))


func test_visual_player_walking() -> void:
	var waypoints: Array[Vector2] = [
		Vector2(100, 300),
		Vector2(250, 150),
		Vector2(700, 250),
		Vector2(750, 450)
	]

	_env.set_server_property(&"position", waypoints[0])
	await _harness.yield_to_sync()
	
	var walk_speed := 6.0
	var network_tick_rate := 6
	
	var current_pos = waypoints[0]

	for i in range(1, waypoints.size()):
		var target = waypoints[i]
		
		while current_pos.distance_to(target) > 0.1:
			# Calculate the chunk of movement for this network tick
			var step_distance = walk_speed * network_tick_rate
			current_pos = current_pos.move_toward(target, step_distance)
			_env.set_server_property(&"position", current_pos)
			await _harness.sync_ticks(network_tick_rate) 

	await _harness.sync_ticks(15)
	
	var final_client_pos = _env.get_client_property(&"position")
	assert_vector(final_client_pos).is_equal_approx(
		waypoints.back(), Vector2(5, 5))
