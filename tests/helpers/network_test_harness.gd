class_name NetworkTestHarness
extends Node

## Test helper that spins up 1 server + N clients in-process using a fresh
## LocalLoopbackSession (never the shared singleton, so tests are isolated).

var _session: LocalLoopbackSession
var _server: MultiplayerTree
var _clients: Array[MultiplayerTree] = []
var _scene_manager_src: Variant
var _world_scene: PackedScene


const DEFAULT_TIMEOUT := 1.0


## Helper to await a signal with the harness's default timeout.
## Returns true if it timed out, false otherwise.
func wait_for(target_signal: Signal, timeout: float = DEFAULT_TIMEOUT) -> bool:
	var timer := get_tree().create_timer(timeout)
	return await Async.timeout(target_signal, timer)


## Creates a fresh session and a server node. Does NOT host yet - register
## spawnable scenes on [method _get_scene_manager] before calling add_client().
## Must be awaited - waits one frame for _ready() to fire before returning.
##
## [param scene_manager_src] accepts:
## [br]- [PackedScene]: instantiated to produce a [MultiplayerSceneManager].
## [br]- [Callable]: called to produce a [MultiplayerSceneManager] (e.g.
##   [method NetworkedTestSuite.create_scene_manager]).
## [br]- [code]null[/code]: no scene manager is created (used by sceneless
##   join tests).
func setup(scene_manager_src: Variant = null, world_scene: PackedScene = null) -> void:
	_scene_manager_src = scene_manager_src
	_world_scene = world_scene
	_session = LocalLoopbackSession.new()
	_setup_server()
	await get_tree().process_frame


## Creates a new client, connects it to the server, and returns it.
## Hosts the server automatically on the first call (after setup() returns,
## giving tests a chance to register spawnable scenes first).
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


## Cleans up all server/client instances and removes nodes from the tree.
## Should be called in after_test().
##
## Frees nodes before closing peers so each synchronizer's
## [code]_exit_tree[/code] (and the engine's [code]on_replication_stop[/code])
## fires while [code]recv_sync_ids[/code] entries are still consistent,
## avoiding stray "missing node" warnings from in-flight sync packets.
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
		await NetworkedTestSuite.drain_frames(tree, 1)

	if _session:
		_session.reset()

	if is_inside_tree():
		get_parent().remove_child(self)

	if tree:
		await NetworkedTestSuite.drain_frames(tree, 2)

	queue_free()


func get_server() -> MultiplayerTree:
	return _server


func get_all_clients() -> Array[MultiplayerTree]:
	return _clients


func get_session() -> LocalLoopbackSession:
	return _session


## Returns the spawned player node name for [param client].
func client_player_name(client: MultiplayerTree) -> StringName:
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	return NetwEntity.format_name(username, peer_id)


## Returns a scene from the server's scene manager by name,
## or the first scene if name is empty.
func get_server_scene(scene_name: StringName = "") -> MultiplayerScene:
	var server_sm := _get_scene_manager(_server)
	if scene_name.is_empty():
		return server_sm.active_scenes.values()[0]
	return server_sm.active_scenes.get(scene_name)


## Admits [param client] to [param scene_name] on the server by calling
## [method MultiplayerScene.connect_peer] directly, bypassing the
## player-join flow. Useful for tests that need to assert client-side
## visibility into a scene without spawning a player into it.
##
## Awaits until the client's [MultiplayerSceneManager] reports the
## scene as active so callers can assert on the replicated state.
func admit_client_to_scene(
	client: MultiplayerTree,
	scene_name: StringName,
) -> MultiplayerScene:
	var server_scene := get_server_scene(scene_name)
	assert(server_scene, "admit_client_to_scene: scene '%s' not active on server." % scene_name)
	var peer_id := client.multiplayer_peer.get_unique_id()
	server_scene.connect_peer(peer_id)
	return await wait_for_client_scene_spawn(client, scene_name)


## Sends the real request_join_player RPC from a client to the server,
## triggering the full _on_player_joined production chain.
## level_scene_path must be a registered spawnable scene whose filename (no extension)
## matches the level root node name (e.g. "TestLevel.tscn" -> root "TestLevel").
## spawner_node_path is relative to the level root (e.g. "TestPlayerFull/SpawnerComponent").
## Returns the spawned player node from the server scene after one process frame.
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
	var scene := get_server_scene(scene_name)
	var player_name := client_player_name(client)
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

	var scene := get_server_scene(scene_name)
	scene.add_player(player)
	return player


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


func wait_for_client_scene_spawn(client: MultiplayerTree, scene_name: StringName) -> MultiplayerScene:
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
func wait_for_client_player_spawn(
	client: MultiplayerTree,
	scene_name: StringName,
	player_name: StringName = &"",
) -> Node:
	var scene := await wait_for_client_scene_spawn(client, scene_name)
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


# Finds a tracked player by node name.
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
