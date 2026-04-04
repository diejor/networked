class_name NetworkTestHarness
extends Node

## Test helper that spins up 1 server + N clients in-process using a fresh
## LocalLoopbackSession (never the shared singleton, so tests are isolated).

var _session: LocalLoopbackSession
var _server: MultiplayerTree
var _clients: Array[MultiplayerTree] = []
var _lobby_manager_scene: PackedScene
var _n_clients: int = 0


func _init() -> void:
	NetLog.current_level = NetLog.Level.NONE


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

## Creates a fresh session, adds server + n_clients nodes to the tree.
## Must be awaited — waits one frame for _ready() to complete before returning.
func setup(n_clients: int, lobby_manager_scene: PackedScene) -> void:
	_n_clients = n_clients
	_lobby_manager_scene = lobby_manager_scene
	_session = LocalLoopbackSession.new()

	_setup_server()
	for i in n_clients:
		_setup_client(i)

	await get_tree().process_frame


## Hosts the server then joins each client sequentially.
## join() internally awaits connected_to_server, so all clients are connected
## by the time this returns.
func connect_all() -> void:
	var host_err: Error = _server.host()
	assert(host_err == OK, "Server host() failed: %s" % error_string(host_err))

	for i in _clients.size():
		var client := _clients[i]
		var join_err: Error = await client.join("localhost", "test_player_%d" % i)
		assert(join_err == OK, "Client %d join() failed: %s" % [i, error_string(join_err)])

	# Wait for server to recognize all peers in its replication interface
	var server_api := _server.multiplayer_api
	while server_api.get_peers().size() < _clients.size():
		await get_tree().process_frame
	
	# Give Godot one extra frame to settle internal peers_info maps
	await get_tree().process_frame


## Cleans up all server/client instances and removes nodes from the tree.
## Should be called in after_test().
func teardown() -> void:
	if is_instance_valid(_server):
		_server.queue_free()
		
	for client in _clients:
		if is_instance_valid(client):
			client.queue_free()
	
	_clients.clear()
	_server = null
	
	if is_inside_tree():
		get_parent().remove_child(self)
	queue_free()


## Awaits until every client has emitted configured.
## Call after connect_all() when you need to confirm lobby managers are ready.
func all_clients_configured() -> void:
	if _n_clients == 0:
		return

	var pending := [_n_clients]
	for client in _clients:
		client.configured.connect(func() -> void: pending[0] -= 1, CONNECT_ONE_SHOT)

	while pending[0] > 0:
		await get_tree().process_frame


func get_server() -> MultiplayerTree:
	return _server


func get_client(i: int) -> MultiplayerTree:
	return _clients[i]


func get_all_clients() -> Array[MultiplayerTree]:
	return _clients


## Returns a lobby from the server's lobby manager by name,
## or the first lobby if name is empty.
func get_server_lobby(lobby_name: StringName = "") -> Lobby:
	var server_mgr: MultiplayerLobbyManager = _server.lobby_manager
	if lobby_name.is_empty():
		return server_mgr.active_lobbies.values()[0]
	return server_mgr.active_lobbies.get(lobby_name)


## Sends the real request_join_player RPC from a client to the server,
## triggering the full _on_player_joined production chain.
## level_scene_path must be a registered spawnable scene whose filename (no extension)
## matches the level root node name (e.g. "TestLevel.tscn" → root "TestLevel").
## spawner_node_path is relative to the level root (e.g. "TestPlayerFull/ClientComponent").
## Returns the spawned player node from the server lobby after one process frame.
func join_player(client_index: int, level_scene_path: String, spawner_node_path: String) -> Node:
	var client := get_client(client_index)
	var username := "test_player_%d" % client_index

	var spawner_path := SceneNodePath.new()
	spawner_path.scene_path = level_scene_path
	spawner_path.node_path = spawner_node_path

	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.spawner_path = spawner_path

	client.lobby_manager.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		client_data.serialize()
	)

	await get_tree().process_frame

	var lobby_name: StringName = spawner_path.get_scene_name()
	var lobby := get_server_lobby(lobby_name)
	var peer_id := client.multiplayer_peer.get_unique_id()
	return lobby.level.get_node_or_null("%s|%d" % [username, peer_id])


## Spawns a player into a server lobby, bypassing the RPC chain.
## Returns the spawned player node.
func spawn_player(client_index: int, player_scene: PackedScene, lobby_name: StringName = "") -> Node:
	var client := get_client(client_index)
	var peer_id := client.multiplayer_peer.get_unique_id()
	var username := "test_player_%d" % client_index

	var player := player_scene.instantiate()
	player.name = "%s|%d" % [username, peer_id]
	var client_comp: ClientComponent = player.get_node("%ClientComponent")
	client_comp.username = username

	var lobby := get_server_lobby(lobby_name)
	lobby.add_player(player)
	return player


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

func _setup_server() -> void:
	_server = MultiplayerTree.new()
	_server.name = "HarnessServer"
	_server.is_server = true
	add_child(_server)

	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	_server.backend = backend

	if _lobby_manager_scene:
		var mgr: MultiplayerLobbyManager = _lobby_manager_scene.instantiate()
		_server.add_child(mgr)
		_server.lobby_manager = mgr


func _setup_client(index: int) -> void:
	var client := MultiplayerTree.new()
	client.name = "HarnessClient%d" % index
	client.is_server = false
	add_child(client)

	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	client.backend = backend

	if _lobby_manager_scene:
		var mgr: MultiplayerLobbyManager = _lobby_manager_scene.instantiate()
		client.add_child(mgr)
		client.lobby_manager = mgr

	_clients.append(client)


func wait_for_client_lobby_spawn(client_index: int, lobby_name: StringName) -> Lobby:
	var client := get_client(client_index)
	while not client.lobby_manager.active_lobbies.has(lobby_name):
		await get_tree().process_frame
	return client.lobby_manager.active_lobbies.get(lobby_name)


func wait_for_client_player_spawn(client_index: int, lobby_name: StringName) -> Node:
	var lobby := await wait_for_client_lobby_spawn(client_index, lobby_name)
	if lobby.synchronizer.tracked_nodes.size() > 0:
		return lobby.synchronizer.tracked_nodes.keys()[0]
	return await lobby.synchronizer.spawned
