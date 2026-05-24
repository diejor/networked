## Multi-peer test rig: one server + N clients in a single SceneTree, all
## wired through a fresh [LocalLoopbackSession] (never the shared singleton,
## so tests stay isolated).
##
## Usage:
## [codeblock]
## var harness := auto_free(NetwTestHarness.new())
## add_child(harness)
## await harness.setup(NetwTestSuite.create_scene_manager(), preload("res://addons/networked_test/fixtures/TestLevel.tscn"))
## var client := await harness.add_client()
## var player := await harness.join_player(client, "uid://...", "Player/Components/SpawnerComponent")
## [/codeblock]
class_name NetwTestHarness
extends Node

const DEFAULT_TIMEOUT := 1.0

## Awaiter contract: [code]func(Signal, float, String) -> bool[/code] —
## returns [code]true[/code] on timeout, [code]false[/code] on success.
## Defaults to a [code]push_error[/code]-backed implementation so the
## harness works in plain Godot without a test framework.
## [method NetwTestSuite.make_harness] swaps in the GdUnit4-aware
## adapter automatically.
var awaiter: Callable = _default_awaiter

var _session: LocalLoopbackSession
var _server: MultiplayerTree
var _clients: Array[MultiplayerTree] = []
var _scene_manager_src: Variant
var _world_scene: PackedScene


# region: generic awaits ------------------------------------------------------

## Awaits [param target_signal] with the harness's default timeout.
## Returns [code]true[/code] on timeout, [code]false[/code] on success.
## Timeouts are reported via [member awaiter] — the GdUnit4 adapter
## surfaces them as test failures.
func wait_for(
	target_signal: Signal,
	timeout: float = DEFAULT_TIMEOUT,
	label: String = "",
) -> bool:
	return await awaiter.call(target_signal, timeout, label)


func _default_awaiter(sig: Signal, timeout: float, label: String) -> bool:
	var timer := get_tree().create_timer(timeout)
	var timed_out: bool = await Async.timeout(sig, timer)
	if timed_out:
		var name := label if not label.is_empty() else String(sig.get_name())
		push_error("Timed out waiting for '%s' after %.2fs." % [name, timeout])
	return timed_out


# region: lifecycle -----------------------------------------------------------

## Creates a fresh session and a server node. Does NOT host yet — register
## spawnable scenes on the server's scene manager before calling
## [method add_client]. Must be awaited; waits one frame for [code]_ready[/code]
## to fire before returning.
##
## [param scene_manager_src] accepts:
## [br]- [PackedScene]: instantiated to produce a [MultiplayerSceneManager].
## [br]- [Callable]: called to produce a [MultiplayerSceneManager].
## [br]- [code]null[/code]: no scene manager is created (sceneless join tests).
func setup(scene_manager_src: Variant = null, world_scene: PackedScene = null) -> void:
	_scene_manager_src = scene_manager_src
	_world_scene = world_scene
	_session = LocalLoopbackSession.new()
	_setup_server()
	await get_tree().process_frame


## Cleans up server, clients, and the session. Call in [code]after_test[/code].
##
## Frees nodes before closing peers so each synchronizer's [code]_exit_tree[/code]
## fires while [code]recv_sync_ids[/code] is still consistent, avoiding stray
## "missing node" warnings from in-flight sync packets.
func teardown() -> void:
	var tree := Engine.get_main_loop() as SceneTree

	if is_instance_valid(_server):
		_server.queue_free()

	for client in _clients:
		if is_instance_valid(client):
			client.queue_free()

	_clients.clear()
	_server = null

	if tree:
		await NetwTestSuite.drain_frames(tree, 1)

	if _session:
		_session.reset()

	if is_inside_tree():
		get_parent().remove_child(self)

	if tree:
		await NetwTestSuite.drain_frames(tree, 2)

	queue_free()


# region: peers ---------------------------------------------------------------

## Creates a new client, connects it to the server, and returns it.
## Hosts the server automatically on the first call (after [method setup]
## returns, giving tests a chance to register spawnable scenes first).
func add_client() -> MultiplayerTree:
	if not _server.is_online():
		var host_err: Error = await _server.host()
		assert(host_err == OK, "Server host() failed: %s" % error_string(host_err))

	var index := _clients.size()
	var username := "test_player_%d" % index

	var client := MultiplayerTree.new()
	client.name = "HarnessClient%d" % index
	client.is_server = false
	client.set_meta(&"_harness_username", username)

	if _world_scene:
		client.add_child(_world_scene.instantiate())

	add_child(client)

	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	client.backend = backend

	if _scene_manager_src:
		var sm := _instantiate_scene_manager()
		if sm:
			_configure_client_scene_manager(sm)
			client.add_child(sm)

	_clients.append(client)

	var join_err: Error = await client.join("localhost", username)
	assert(join_err == OK, "Client %d join() failed: %s" % [index, error_string(join_err)])

	# Wait for server to register this peer
	var peer_id := client.multiplayer_peer.get_unique_id()
	var server_api := _server.multiplayer_api

	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while not peer_id in server_api.get_peers():
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			assert(false, "Timed out waiting for server to register peer %d" % peer_id)

	await get_tree().process_frame

	return client


## Returns the server [MultiplayerTree].
func server() -> MultiplayerTree:
	return _server


## Returns all connected client trees in connection order.
func clients() -> Array[MultiplayerTree]:
	return _clients


## Returns the shared [LocalLoopbackSession] used by server and clients.
func session() -> LocalLoopbackSession:
	return _session


# region: player flows --------------------------------------------------------

## Returns the spawned player node name for [param client].
func player_name_for(client: MultiplayerTree) -> StringName:
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	return NetwEntity.format_name(username, peer_id)


## Admits [param client] to [param scene_name] on the server by calling
## [method MultiplayerScene.connect_peer] directly, bypassing the
## player-join flow. Useful when a test must assert client-side visibility
## without spawning a player into the scene.
##
## Awaits until the client's [MultiplayerSceneManager] reports the scene
## as active.
func admit_client_to_scene(
	client: MultiplayerTree,
	scene_name: StringName,
) -> MultiplayerScene:
	var s := scene_on_server(scene_name)
	assert(s, "admit_client_to_scene: scene '%s' not active on server." % scene_name)
	var peer_id := client.multiplayer_peer.get_unique_id()
	s.connect_peer(peer_id)
	return await wait_for_scene(client, scene_name)


## Sends the real [code]request_join_player[/code] RPC from [param client]
## to the server, triggering the full [code]_on_player_joined[/code]
## production chain.
##
## [param level_scene_path] must be a registered spawnable scene whose
## filename (no extension) matches the level root node name (e.g.
## [code]"TestLevel.tscn"[/code] -> root [code]"TestLevel"[/code]).
## [param spawner_node_path] is relative to the level root
## (e.g. [code]"TestPlayerFull/SpawnerComponent"[/code]).
##
## Returns the spawned player node from the server scene.
func join_player(client: MultiplayerTree, level_scene_path: String, spawner_node_path: String) -> Node:
	var username: String = client.get_meta(&"_harness_username")

	var spawner_component_path := SceneNodePath.new()
	spawner_component_path.scene_path = level_scene_path
	spawner_component_path.node_path = spawner_node_path

	var join_payload := JoinPayload.new()
	join_payload.username = username
	join_payload.spawner_component_path = spawner_component_path

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize()
	)

	var scene_name: StringName = spawner_component_path.get_scene_name()
	var scene := scene_on_server(scene_name)
	var player_name := player_name_for(client)
	var player_path := NodePath(String(player_name))

	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while scene.level.get_node_or_null(player_path) == null:
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			assert(false, "Timed out waiting for player '%s' to spawn in scene '%s'." % [player_name, scene_name])
			return null

	return scene.level.get_node_or_null(player_path)


## Spawns a player into a server scene, bypassing the RPC chain.
## Returns the spawned player node.
func spawn_player(client: MultiplayerTree, player_scene: PackedScene, scene_name: StringName = "") -> Node:
	var peer_id := client.multiplayer_peer.get_unique_id()
	var username: String = client.get_meta(&"_harness_username")

	var player := player_scene.instantiate()
	NetwEntity.bundle(player, peer_id, StringName(username))

	var scene := scene_on_server(scene_name)
	scene.add_player(player)
	return player


# region: scene configuration ------------------------------------------------

## Configures the server's lifecycle policy for [param scene_name],
## forwarding to [method MultiplayerSceneManager.set_scene_lifecycle_policy].
##
## [b]Call window:[/b] after [method setup] returns and [i]before[/i] the
## first [method add_client] call. At that point the server's scene manager
## exists but no peers have been registered, so the policy applies cleanly
## to every subsequent join. Calling it later still works, but only affects
## scenes spawned afterward.
func set_scene_policy(
	scene_name: StringName,
	load_mode: MultiplayerSceneManager.LoadMode,
	empty_action: MultiplayerSceneManager.EmptyAction,
) -> void:
	var sm := _get_scene_manager(_server)
	assert(sm, "set_scene_policy: server has no MultiplayerSceneManager.")
	sm.set_scene_lifecycle_policy(scene_name, load_mode, empty_action)


# region: scene waits ---------------------------------------------------------

## Returns the named active scene from the server's scene manager, or the
## first active scene if [param scene_name] is empty.
func scene_on_server(scene_name: StringName = "") -> MultiplayerScene:
	var server_sm := _get_scene_manager(_server)
	if scene_name.is_empty():
		return server_sm.active_scenes.values()[0]
	return server_sm.active_scenes.get(scene_name)


## Waits for [param scene_name] to become active on [param client]'s
## scene manager.
func wait_for_scene(client: MultiplayerTree, scene_name: StringName) -> MultiplayerScene:
	var sm := _get_scene_manager(client)
	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while not sm.active_scenes.has(scene_name):
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			assert(false, "Timed out waiting for scene '%s' to spawn on client." % [scene_name])
	return sm.active_scenes.get(scene_name)


## Waits for a player in [param scene_name] on [param client].
##
## When [param player_name] is empty, returns the first tracked player.
func wait_for_player(
	client: MultiplayerTree,
	scene_name: StringName,
	player_name: StringName = &"",
) -> Node:
	var scene := await wait_for_scene(client, scene_name)
	if not scene:
		return null
	if player_name.is_empty():
		var existing := scene.player_nodes()
		if existing.size() > 0:
			return existing[0]
	else:
		var existing_player := _find_scene_player(scene, player_name)
		if existing_player:
			return existing_player

	var timeout_timer := get_tree().create_timer(DEFAULT_TIMEOUT)
	while true:
		if player_name.is_empty():
			var players := scene.player_nodes()
			if players.size() > 0:
				return players[0]
		else:
			var named_player := _find_scene_player(scene, player_name)
			if named_player:
				return named_player
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			assert(false,
				"Timed out waiting for player in scene '%s'." % scene_name)
			return null
	return null


# region: internals -----------------------------------------------------------

func _get_scene_manager(mt: MultiplayerTree) -> MultiplayerSceneManager:
	return mt.get_service(MultiplayerSceneManager)


func _instantiate_scene_manager() -> MultiplayerSceneManager:
	if _scene_manager_src is PackedScene:
		return (_scene_manager_src as PackedScene).instantiate()
	elif _scene_manager_src is Callable:
		return (_scene_manager_src as Callable).call()
	elif _scene_manager_src is MultiplayerSceneManager:
		return _scene_manager_src as MultiplayerSceneManager
	return null


# Mirrors server scene replication config onto a newly created client manager.
func _configure_client_scene_manager(sm: MultiplayerSceneManager) -> void:
	var server_sm := _get_scene_manager(_server)
	if not server_sm:
		return
	var paths: Array[String] = []
	for path: String in server_sm.scene_paths:
		if not paths.has(path):
			paths.append(path)
	for path: String in server_sm._get_configured_paths():
		if not paths.has(path):
			paths.append(path)
	for path: String in paths:
		sm.add_spawnable_scene(path)


func _setup_server() -> void:
	_server = MultiplayerTree.new()
	_server.name = "HarnessServer"
	_server.is_server = true
	_server.auto_host_headless = false

	if _world_scene:
		_server.add_child(_world_scene.instantiate())

	add_child(_server)

	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	_server.backend = backend

	if _scene_manager_src:
		var sm := _instantiate_scene_manager()
		if sm:
			_server.add_child(sm)


func _find_scene_player(
	scene: MultiplayerScene,
	player_name: StringName,
) -> Node:
	if not scene:
		return null
	for player: Node in scene.player_nodes():
		if player.name == player_name:
			return player
	return null
