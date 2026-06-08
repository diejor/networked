## Harness for movement tests requiring a synchronized [MultiplayerClock].
##
## Wraps [NetwTestHarness] and adds [MultiplayerClock] services to all peers.
## Provides [method sync_ticks_real] to reliably advance simulation in CI.
class_name TickNetworkTestHarness
extends Node

const TICKRATE := 30
const DISPLAY_OFFSET := 3
const DELTA_INTERVAL := 0.05

var _runner: GdUnitSceneRunner
var _inner: NetwTestHarness
var _client: MultiplayerTree
var _holding: bool = false


## Creates the wrapped [NetwTestHarness], server clock, and one client.
func setup(runner: GdUnitSceneRunner) -> void:
	_runner = runner
	_inner = NetwTestHarness.new()
	_runner.scene().add_child(_inner)
	await _inner.setup()
	await _inner.add_clock(TICKRATE, DISPLAY_OFFSET)
	_client = await _inner.add_client()


## Tears down the wrapped [NetwTestHarness] and frees this harness.
func teardown() -> void:
	if is_instance_valid(_inner):
		await _inner.teardown()
	if is_inside_tree():
		get_parent().remove_child(self)
	queue_free()


## Returns the server [MultiplayerTree].
func get_server() -> MultiplayerTree:
	return _inner.server()


## Returns the default client [MultiplayerTree].
func get_client() -> MultiplayerTree:
	return _client


func _make_replication_config(prop_path: NodePath) -> SceneReplicationConfig:
	var cfg := SceneReplicationConfig.new()
	cfg.add_property(prop_path)
	cfg.property_set_replication_mode(
		prop_path,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)
	cfg.property_set_spawn(prop_path, false)
	cfg.property_set_watch(prop_path, true)
	return cfg


func _build_server_node(
		node_name: StringName,
		prop_path: NodePath,
		extra_child: Node = null,
) -> Node2D:
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
	sync.owner = player

	return player


func _build_client_node(
		node_name: StringName,
		prop_path: NodePath,
		interp_props: Dictionary,
		extra_child: Node = null,
) -> Node2D:
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
	sync.owner = player

	var interp := MultiplayerInterpolator.new()
	interp.name = "MultiplayerInterpolator"
	interp.trace_interval = 1
	# Keeps the exported typed dictionary assignment valid.
	var modes: Dictionary[StringName, MultiplayerInterpolator.Mode] = { }
	for key in interp_props:
		modes[key] = interp_props[key]
	interp.property_modes = modes
	player.add_child(interp)
	interp.owner = player

	return player


## Creates synchronized server/client [Node2D] pairs for position tests.
func create_environment(node_name: StringName) -> TickSimulationEnvironment:
	var env := TickSimulationEnvironment.new()

	env.server_node = _build_server_node(node_name, NodePath(".:position"))
	get_server().add_child(env.server_node)

	var interp_props = { &"position": MultiplayerInterpolator.Mode.LERP }
	env.client_node = _build_client_node(
		node_name,
		NodePath(".:position"),
		interp_props,
	)
	get_client().add_child(env.client_node)

	env.interpolator = env.client_node.get_node("MultiplayerInterpolator")

	await get_tree().process_frame
	return env


## Creates synchronized server/client [Sprite2D] pairs for modulate tests.
func create_environment_with_sprite(
		node_name: StringName,
) -> TickSimulationEnvironment:
	var env := TickSimulationEnvironment.new()

	var s_sprite := Sprite2D.new()
	s_sprite.name = "Sprite2D"
	env.server_node = _build_server_node(
		node_name,
		NodePath("Sprite2D:modulate"),
		s_sprite,
	)
	get_server().add_child(env.server_node)

	var c_sprite := Sprite2D.new()
	c_sprite.name = "Sprite2D"
	var interp_props = { &"Sprite2D:modulate": MultiplayerInterpolator.Mode.LERP }
	env.client_node = _build_client_node(
		node_name,
		NodePath("Sprite2D:modulate"),
		interp_props,
		c_sprite,
	)
	get_client().add_child(env.client_node)

	env.interpolator = env.client_node.get_node("MultiplayerInterpolator")

	await get_tree().process_frame
	return env


## Advances the simulation by [param n] network ticks.
func sync_ticks(n: int) -> void:
	await sync_ticks_real(n)


## Advances the simulation by exactly [param n] network ticks.
## [br][br]
## This is more reliable than [method GdUnitSceneRunner.simulate_frames] in CI
## because it anchors to the actual simulation clock signals.
func sync_ticks_real(n: int) -> void:
	var clock := _inner.server().get_service(MultiplayerClock) as MultiplayerClock
	if not clock:
		@warning_ignore("redundant_await")
		await _runner.simulate_frames(n)
		return

	var target_tick := clock.tick + n

	# Calculates approximate frames needed: ticks * physics_fps / tickrate.
	var physics_fps: int = ProjectSettings.get_setting(
		"physics/common/physics_ticks_per_second",
		60,
	)
	var estimated_frames := ceili(
		float(n) * float(physics_fps) / float(TICKRATE),
	)

	# Simulates the bulk of frames in one call.
	if estimated_frames > 2:
		@warning_ignore("redundant_await")
		await _runner.simulate_frames(estimated_frames - 2)

	# Fine-tunes the last few frames to hit exactly target_tick.
	var timeout := 100
	while clock.tick < target_tick and timeout > 0:
		@warning_ignore("redundant_await")
		await _runner.simulate_frames(1)
		timeout -= 1


## Awaits until the client [MultiplayerClock] synchronizes with the server.
func wait_for_clock_sync(timeout_ticks: int = 100) -> void:
	var client_clock := _client.get_service(MultiplayerClock) as MultiplayerClock
	var timeout := timeout_ticks

	while not client_clock.is_synchronized and timeout > 0:
		# Simulates in small batches so handshake RPCs can complete.
		@warning_ignore("redundant_await")
		await _runner.simulate_frames(2)
		timeout -= 1

	assert(
		client_clock.is_synchronized,
		"Timed out waiting for MultiplayerClock synchronization",
	)


## Advances enough ticks for replicated values to reach the display tick.
func yield_to_sync(extra_frames: int = 0) -> void:
	# Covers the display offset plus the replication interval.
	var needed_ticks := DISPLAY_OFFSET + ceili(DELTA_INTERVAL * TICKRATE) + \
			4 + extra_frames
	await sync_ticks_real(needed_ticks)


## Sets the [GdUnitSceneRunner] time scale.
func set_time_factor(factor: float) -> void:
	_runner.set_time_factor(factor)


## Moves the runner window to the foreground.
func show_window() -> void:
	_runner.move_window_to_foreground()


## Holds inbound packets to the default client.
func hold_client_packets() -> void:
	_holding = true
	_inner.hold_packets_to_client(_client)


## Releases packets held by [method hold_client_packets].
func release_client_packets() -> void:
	if not _holding:
		return
	_holding = false
	_inner.release_packets_to_client(_client)
