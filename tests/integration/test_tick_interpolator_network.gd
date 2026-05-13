## Integration tests for [TickInterpolator] with real in-process networking.
class_name TestTickInterpolatorNetwork
extends NetworkedTestSuite

const DELTA_INTERVAL := 0.05
const TICKRATE       := 30
const DISPLAY_OFFSET := 3
const CONVERGE_WAIT  := 0.3

var _harness: NetworkTestHarness
var _client:  MultiplayerTree

var _server_player: Node2D
var _client_player:  Node2D


func before_test() -> void:
	_harness = auto_free(NetworkTestHarness.new())
	add_child(_harness)
	await _harness.setup()

	_add_clock(_harness.get_server())

	_client = await _harness.add_client()

	var client_clock := _add_clock(_client)
	client_clock._on_tree_configured()
	
	if not client_clock.is_synchronized:
		await timeout_await(client_clock.clock_synchronized)

	_server_player = _build_server_node()
	_harness.get_server().add_child(_server_player)

	_client_player = _build_client_node()
	_client.add_child(_client_player)

	await get_tree().process_frame


func after_test() -> void:
	if is_instance_valid(_harness):
		await _harness.teardown()
	await drain_frames(get_tree(), 3)


func _add_clock(tree: MultiplayerTree) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.name   = "NetworkClock"
	clock.tickrate       = TICKRATE
	clock.display_offset = DISPLAY_OFFSET
	tree.add_child(clock)
	return clock


func _make_replication_config() -> SceneReplicationConfig:
	var cfg   := SceneReplicationConfig.new()
	var ppath := NodePath(".:position")
	cfg.add_property(ppath)
	cfg.property_set_replication_mode(
		ppath, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	cfg.property_set_spawn(ppath, false)
	cfg.property_set_watch(ppath, true)
	return cfg


func _build_server_node() -> Node2D:
	var player := Node2D.new()
	player.name = "InterpTestPlayer"
	player.set_multiplayer_authority(1)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.replication_config = _make_replication_config()
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	return player


func _build_client_node() -> Node2D:
	var player := Node2D.new()
	player.name = "InterpTestPlayer"
	player.set_multiplayer_authority(1)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.replication_config = _make_replication_config()
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	var interp := TickInterpolator.new()
	interp.name = "TickInterpolator"
	interp.property_modes = {&"position": TickInterpolator.Mode.LERP}
	interp.trace_interval = 1
	player.add_child(interp)

	return player


func _wait_until_converged(
	node: Node2D,
	target: Vector2,
	timeout: float = 1.0
) -> bool:
	var start_time := Time.get_ticks_msec()
	while node.position.distance_to(target) > 5.0:
		if Time.get_ticks_msec() - start_time > timeout * 1000:
			return false
		await get_tree().process_frame
	return true


func test_remote_player_converges_to_server_position() -> void:
	var target := Vector2(300.0, 0.0)
	_server_player.position = target

	var ok := await _wait_until_converged(_client_player, target, CONVERGE_WAIT)
	assert_bool(ok).is_true()


func test_teleport_snaps_instead_of_lerping() -> void:
	var interp: TickInterpolator = _client_player.get_node("TickInterpolator")
	interp.max_lerp_distance = 100.0 # Anything over 100 units should snap.
	
	const START := Vector2(0.0, 0.0)
	const JUMP  := Vector2(1000.0, 0.0)
	
	_server_player.position = START
	await _wait_until_converged(_client_player, START, CONVERGE_WAIT)
	
	_server_player.position = JUMP
	
	# Wait for the synchronizer to fire (DELTA_INTERVAL = 50ms)
	await get_tree().create_timer(0.06).timeout
	
	# Wait for display lag (100ms) to pass.
	await get_tree().create_timer(0.1).timeout
	
	assert_vector(_client_player.position).is_equal_approx(
		JUMP, Vector2(1.0, 1.0))


func test_authority_handover_disables_interpolation() -> void:
	const START := Vector2(0.0, 0.0)
	const CLIENT_MOVE := Vector2(50.0, 50.0)

	_server_player.position = START
	await _wait_until_converged(_client_player, START, CONVERGE_WAIT)
	
	var client_id := _client.multiplayer.get_unique_id()
	_server_player.set_multiplayer_authority(client_id)
	_client_player.set_multiplayer_authority(client_id)
	
	_client_player.position = CLIENT_MOVE
	
	for _i in 10:
		await get_tree().process_frame
		
	assert_vector(_client_player.position).is_equal(CLIENT_MOVE)
