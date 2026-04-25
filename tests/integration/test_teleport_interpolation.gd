class_name TestTeleportInterpolation
extends NetworkedTestSuite

var _runner: GdUnitSceneRunner
var _harness: TickNetworkTestHarness
var _env: TickSimulationEnvironment

func before_test() -> void:
	_runner = scene_runner("res://tests/helpers/tick_test_stage.tscn")
	_harness = auto_free(TickNetworkTestHarness.new())
	
	add_child(_harness)
	await _harness.setup(_runner)
	_env = await _harness.create_environment(&"TeleportPlayer")

	var interpolator: TickInterpolator = _env.client_node.get_node("TickInterpolator")
	interpolator.trace_interval = 1

	_harness.set_time_factor(2.0)
func after_test() -> void:
	if is_instance_valid(_harness):
		_harness.teardown()
	await get_tree().process_frame

func test_teleport_with_smoothing_does_not_glide() -> void:
	var interpolator: TickInterpolator = _env.client_node.get_node("TickInterpolator")
	interpolator.max_lerp_distance = 100.0
	interpolator.smoothing = 0.9 # Very heavy smoothing

	const P1 := Vector2(0, 0)
	const P2 := Vector2(500, 500) # Far away, > 100

	# 1. Stabilize at P1
	_env.server_node.position = Vector2(-100, -100) # Different from P1
	await _harness.sync_ticks(5)
	_env.set_server_property(&"position", P1)
	
	# Wait until client has some display history
	await wait_until(func(): return interpolator._clock.display_tick > 10, 2.0)
	await _harness.sync_ticks(10)
	await _harness.yield_to_sync()
	
	assert_vector(_env.client_node.position).is_equal_approx(P1, Vector2(1, 1))

	# 2. Server teleports to P2
	_env.server_node.position = P2
	_env.set_server_property(&"position", P2)
	
	# Wait for sync
	await _harness.sync_ticks(5)
	
	# Wait enough for display_tick to reach the teleport.
	await _harness.sync_ticks(10)
	
	var pos = _env.client_node.position
	var dist_to_p2 = pos.distance_to(P2)
	
	assert_float(dist_to_p2).is_less(0.1)
