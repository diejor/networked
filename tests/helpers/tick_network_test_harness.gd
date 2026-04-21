class_name TickNetworkTestHarness
extends Node

const TICKRATE       := 30
const DISPLAY_OFFSET := 3
const DELTA_INTERVAL := 0.05

var _runner: GdUnitSceneRunner
var _inner: NetworkTestHarness
var _client: MultiplayerTree
var _held_packets: Array[Dictionary] = []
var _holding: bool = false

func setup(runner: GdUnitSceneRunner) -> void:
	_runner = runner
	_inner = NetworkTestHarness.new()
	_runner.scene().add_child(_inner)
	await _inner.setup()
	_add_clock(_inner.get_server())
	_client = await _inner.add_client()
	var client_clock := _add_clock(_client)
	client_clock._on_tree_configured()

func teardown() -> void:
	if is_instance_valid(_inner):
		_inner.teardown()
	if is_inside_tree():
		get_parent().remove_child(self)
	queue_free()

func get_server() -> MultiplayerTree:
	return _inner.get_server()

func get_client() -> MultiplayerTree:
	return _client

func _add_clock(tree: MultiplayerTree) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.name   = "NetworkClock"
	clock.tickrate       = TICKRATE
	clock.display_offset = DISPLAY_OFFSET
	tree.add_child(clock)
	return clock

func _make_replication_config(prop_path: NodePath) -> SceneReplicationConfig:
	var cfg := SceneReplicationConfig.new()
	cfg.add_property(prop_path)
	cfg.property_set_replication_mode(prop_path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	cfg.property_set_spawn(prop_path, false)
	cfg.property_set_watch(prop_path, true)
	return cfg

func _build_server_node(node_name: StringName, prop_path: NodePath, extra_child: Node = null) -> Node2D:
	var player := Node2D.new()
	player.name = node_name
	player.set_multiplayer_authority(1)

	var rect := ColorRect.new()
	rect.color = Color.GREEN
	rect.custom_minimum_size = Vector2(16, 16)
	rect.position = Vector2(-8, -8)
	player.add_child(rect)

	if extra_child:
		player.add_child(extra_child)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.replication_config = _make_replication_config(prop_path)
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	return player

func _build_client_node(node_name: StringName, prop_path: NodePath, interp_props: Dictionary, extra_child: Node = null) -> Node2D:
	var player := Node2D.new()
	player.name = node_name
	player.set_multiplayer_authority(1)

	var rect := ColorRect.new()
	rect.color = Color.RED
	rect.custom_minimum_size = Vector2(16, 16)
	rect.position = Vector2(-8, -8)
	player.add_child(rect)

	if extra_child:
		player.add_child(extra_child)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.replication_config = _make_replication_config(prop_path)
	sync.delta_interval = DELTA_INTERVAL
	player.add_child(sync)

	var interp := TickInterpolator.new()
	interp.name = "TickInterpolator"
	# Initialize typed dictionary to avoid assignment mismatch
	var modes: Dictionary[StringName, TickInterpolator.Mode] = {}
	for key in interp_props:
		modes[key] = interp_props[key]
	interp.property_modes = modes
	player.add_child(interp)

	return player

func create_environment(node_name: StringName) -> TickSimulationEnvironment:
	var env := TickSimulationEnvironment.new()
	
	env.server_node = _build_server_node(node_name, NodePath(".:position"))
	get_server().add_child(env.server_node)

	var interp_props = {&"position": TickInterpolator.Mode.LERP}
	env.client_node = _build_client_node(node_name, NodePath(".:position"), interp_props)
	get_client().add_child(env.client_node)

	env.interpolator = env.client_node.get_node("TickInterpolator")

	await get_tree().process_frame
	return env

func create_environment_with_sprite(node_name: StringName) -> TickSimulationEnvironment:
	var env := TickSimulationEnvironment.new()
	
	var s_sprite := Sprite2D.new()
	s_sprite.name = "Sprite2D"
	env.server_node = _build_server_node(node_name, NodePath("Sprite2D:modulate"), s_sprite)
	get_server().add_child(env.server_node)

	var c_sprite := Sprite2D.new()
	c_sprite.name = "Sprite2D"
	var interp_props = {&"Sprite2D:modulate": TickInterpolator.Mode.LERP}
	env.client_node = _build_client_node(node_name, NodePath("Sprite2D:modulate"), interp_props, c_sprite)
	get_client().add_child(env.client_node)

	env.interpolator = env.client_node.get_node("TickInterpolator")

	await get_tree().process_frame
	return env

func sync_ticks(n: int) -> void:
	await _runner.simulate_frames(n)

func yield_to_sync(extra_frames: int = 0) -> void:
	var frames := DISPLAY_OFFSET + ceili(DELTA_INTERVAL * TICKRATE) + 4 + extra_frames
	await sync_ticks(frames)

func set_time_factor(factor: float) -> void:
	_runner.set_time_factor(factor)

func show_window() -> void:
	_runner.move_window_to_foreground()

func _process(_delta: float) -> void:
	if not _holding: return
	var peer := _get_client_peer()
	if not peer: return
	_held_packets.append_array(peer._packet_queue)
	peer._packet_queue.clear()

func hold_client_packets() -> void:
	_holding = true

func release_client_packets() -> void:
	if not _holding: return
	_holding = false
	var peer := _get_client_peer()
	if not peer: return
	var existing := peer._packet_queue.duplicate()
	peer._packet_queue.clear()
	peer._packet_queue.append_array(_held_packets)
	peer._packet_queue.append_array(existing)
	_held_packets.clear()

func _get_client_peer() -> LocalMultiplayerPeer:
	var session := _inner.get_session()
	if not session or session.client_peers.is_empty(): return null
	return session.client_peers[0] as LocalMultiplayerPeer
