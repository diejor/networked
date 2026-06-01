## Integration tests for [TickInterpolator] with real in-process networking.
class_name TestTickInterpolatorNetwork
extends NetwTestSuite

var _runner: GdUnitSceneRunner
var _harness: TickNetworkTestHarness
var _env: TickSimulationEnvironment


func before_test() -> void:
	_runner = scene_runner("res://tests/helpers/tick_test_stage.tscn")
	_harness = auto_free(TickNetworkTestHarness.new())

	add_child(_harness)
	await _harness.setup(_runner)
	_env = await _harness.create_environment(&"InterpTestPlayer")
	await _harness.wait_for_clock_sync()

	var interpolator: TickInterpolator = _env.client_node.get_node(
		"TickInterpolator"
	)
	interpolator.trace_interval = 0

	_harness.set_time_factor(10.0)


func after_test() -> void:
	if is_instance_valid(_harness):
		await _harness.teardown()
	await super.after_test()


func _wait_for_client_position(
	target: Vector2,
	tolerance: Vector2,
	max_frames: int = 120,
) -> void:
	for _i in max_frames:
		var pos := _env.get_client_property(&"position") as Vector2
		if pos.is_equal_approx(target) or (
			absf(pos.x - target.x) <= tolerance.x
			and absf(pos.y - target.y) <= tolerance.y
		):
			return
		await _harness.sync_ticks(1)
	fail("Timed out waiting for client position %s." % [target])


func test_remote_player_converges_to_server_position() -> void:
	var target := Vector2(300.0, 0.0)
	_env.set_server_property(&"position", target)

	await _wait_for_client_position(target, Vector2(5.0, 5.0))

	assert_vector(_env.get_client_property(&"position")).is_equal_approx(
		target,
		Vector2(5.0, 5.0)
	)


func test_teleport_snaps_instead_of_lerping() -> void:
	var interpolator: TickInterpolator = _env.interpolator
	interpolator.max_lerp_distance = 100.0

	const START := Vector2(0.0, 0.0)
	const JUMP := Vector2(1000.0, 0.0)

	_env.set_server_property(&"position", START)
	await _wait_for_client_position(START, Vector2(1.0, 1.0))

	_env.set_server_property(&"position", JUMP)
	await _wait_for_client_position(JUMP, Vector2(1.0, 1.0))

	assert_vector(_env.get_client_property(&"position")).is_equal_approx(
		JUMP,
		Vector2(1.0, 1.0)
	)


func test_authority_handover_disables_interpolation() -> void:
	const START := Vector2(0.0, 0.0)
	const CLIENT_MOVE := Vector2(50.0, 50.0)

	_env.set_server_property(&"position", START)
	await _wait_for_client_position(START, Vector2(1.0, 1.0))

	var client_id := _harness.get_client().multiplayer.get_unique_id()
	_env.server_node.set_multiplayer_authority(client_id)
	_env.client_node.set_multiplayer_authority(client_id)

	_env.client_node.position = CLIENT_MOVE

	await _harness.sync_ticks(10)

	assert_vector(_env.client_node.position).is_equal(CLIENT_MOVE)
