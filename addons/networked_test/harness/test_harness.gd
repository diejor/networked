## Multi-peer test rig for Networked multiplayer tests.
##
## Creates one server plus N clients in a single [SceneTree], all wired
## through a fresh [LocalLoopbackSession]. Tests should drive multiplayer
## flows through this public surface instead of reaching into transport or
## scene-manager internals.
##
## Usage:
## [codeblock]
## var harness := make_harness()
## await harness.setup(NetwTestSuite.create_scene_manager)
## harness.register_spawnable_scene(LEVEL)
## var client := await harness.add_client()
## var player := await harness.join_player(
##     client,
##     "uid://...",
##     "Player/Components/SpawnerComponent"
## )
## [/codeblock]
class_name NetwTestHarness
extends Node

const DEFAULT_TIMEOUT := 1.0

## Awaiter contract: [code]func(Signal, float, String) -> bool[/code].
## returns [code]true[/code] on timeout, [code]false[/code] on success.
## Defaults to a [code]push_error[/code]-backed implementation so the
## harness works in plain Godot without a test framework.
## [method NetwTestSuite.make_harness] swaps in the GdUnit4-aware
## adapter automatically.
var awaiter: Callable = _default_awaiter

signal _wait_satisfied()

var _session: LocalLoopbackSession
var _server: MultiplayerTree
var _clients: Array[MultiplayerTree] = []
var _scene_manager_src: Variant
var _world_scene: PackedScene
var _wait_generation: int = 0
var _extra_sessions: Array[LocalLoopbackSession] = []
var _clock_enabled: bool = false
var _clock_tickrate: int = 30
var _clock_display_offset: int = 3
var _torn_down := false


#region Generic awaits

## Awaits [param target_signal] with the harness's default timeout.
## Returns [code]true[/code] on timeout, [code]false[/code] on success.
## Timeouts are reported via [member awaiter]. The GdUnit4 adapter
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


#endregion

#region Lifecycle

## Creates a fresh session and server node.
##
## Does not host yet. Register spawnable scenes with
## [method register_spawnable_scene] before calling [method add_client].
## Must be awaited; waits one frame for [code]_ready[/code]
## to fire before returning.
##
## [param scene_manager_src] accepts:
## [br]- [PackedScene]: instantiated to produce a [MultiplayerSceneManager].
## [br]- [Callable]: called to produce a [MultiplayerSceneManager].
## [br]- [code]null[/code]: no scene manager is created (sceneless join tests).
func setup(
	scene_manager_src: Variant = null,
	world_scene: PackedScene = null,
) -> void:
	_scene_manager_src = scene_manager_src
	_world_scene = world_scene
	_session = LocalLoopbackSession.new()
	_setup_server()
	await get_tree().process_frame


## Cleans up server, clients, and the session.
##
## [NetwTestSuite] calls this automatically for harnesses created through
## [method NetwTestSuite.make_harness]. Direct users may call it explicitly;
## repeated calls are ignored.
##
## Frees nodes before closing peers so each synchronizer's
## [code]_exit_tree[/code] fires while [code]recv_sync_ids[/code] is still
## consistent, avoiding stray "missing node" warnings from in-flight sync
## packets.
func teardown() -> void:
	if _torn_down:
		return
	_torn_down = true

	var tree := Engine.get_main_loop() as SceneTree

	if is_instance_valid(_server):
		_server.queue_free()

	for client in _clients:
		if is_instance_valid(client):
			client.queue_free()

	for child in get_children():
		if child is MultiplayerTree and child != _server:
			if not _clients.has(child) and is_instance_valid(child):
				child.queue_free()

	_clients.clear()
	_server = null

	if tree:
		await NetwTestSuite.drain_frames(tree, 1)

	if _session:
		_session.reset()
	_session = null

	for extra_session in _extra_sessions:
		if extra_session:
			extra_session.reset()
	_extra_sessions.clear()
	_scene_manager_src = null
	_world_scene = null
	awaiter = Callable()

	if is_inside_tree():
		get_parent().remove_child(self)

	if tree:
		await NetwTestSuite.drain_frames(tree, 2)

	queue_free()


#endregion

#region Peers

## Creates a new client, connects it to the server, and returns it.
## Hosts the server automatically on the first call so tests have a chance
## to call [method register_spawnable_scene] after [method setup] returns.
func add_client() -> MultiplayerTree:
	await _ensure_server_hosted()

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
	if _clock_enabled:
		_add_clock_node(client)

	var payload := make_join_payload(username)
	var target := JoinTarget.new()
	target.backend = client.backend
	target.address = "localhost"
	var join_err: Error = await client.join(target, payload)
	assert(
		join_err == OK,
		"Client %d join() failed: %s" % [index, error_string(join_err)],
	)

	var peer_id := client.multiplayer_peer.get_unique_id()
	var server_api := _server.multiplayer_api
	await _wait_until(
		func() -> bool: return peer_id in server_api.get_peers(),
		"server to register peer %d" % peer_id,
	)

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


## Hosts the server tree if it is not already online.
func host_server() -> void:
	await _ensure_server_hosted()


## Returns the [MultiplayerSceneManager] service for [param mt].
func scene_manager_for(mt: MultiplayerTree) -> MultiplayerSceneManager:
	return mt.get_service(MultiplayerSceneManager)


## Returns the server [MultiplayerSceneManager] service.
func server_scene_manager() -> MultiplayerSceneManager:
	return scene_manager_for(_server)


## Creates a [NetworkClock] on the server and all clients.
##
## Clients created after this call receive the same clock before joining.
## Existing clients are awaited until [signal NetworkClock.clock_synchronized]
## fires.
func add_clock(
	tickrate: int = 30,
	display_offset: int = 3,
) -> NetworkClock:
	_clock_enabled = true
	_clock_tickrate = tickrate
	_clock_display_offset = display_offset

	var server_clock := _ensure_clock(_server)
	for client in _clients:
		var client_clock := _ensure_clock(client)
		if not client_clock.is_synchronized:
			await wait_for(
				client_clock.clock_synchronized,
				DEFAULT_TIMEOUT,
				"client clock synchronization"
			)
	return server_clock


## Holds inbound packets to [param client] until
## [method release_packets_to_client] is called.
func hold_packets_to_client(client: MultiplayerTree) -> void:
	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	_session.hold_inbound_packets(peer)


## Releases packets held by [method hold_packets_to_client].
func release_packets_to_client(client: MultiplayerTree) -> void:
	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	_session.release_inbound_packets(peer)


## Disconnects [param client] from the harness server without freeing it.
## The client can be passed to [method reconnect_client] afterward.
func disconnect_client(client: MultiplayerTree) -> void:
	assert(_clients.has(client), "disconnect_client: unknown client tree.")
	if not client.multiplayer_peer:
		return

	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	var peer_id := client.multiplayer_peer.get_unique_id()
	client.state = MultiplayerTree.State.DISCONNECTING
	if peer:
		_session.release_inbound_packets(peer)
	client.multiplayer_peer.close()

	var server_api := _server.multiplayer_api
	await _wait_until(
		func() -> bool: return not peer_id in server_api.get_peers(),
		"server to unregister peer %d" % peer_id,
	)

	if client.api and client.api.has_multiplayer_peer():
		client.api.multiplayer_peer = null
	client.state = MultiplayerTree.State.OFFLINE
	client.role = MultiplayerTree.Role.NONE
	await get_tree().process_frame


## Reconnects a client previously closed by [method disconnect_client].
func reconnect_client(client: MultiplayerTree) -> void:
	assert(_clients.has(client), "reconnect_client: unknown client tree.")
	await _ensure_server_hosted()

	var username: String = client.get_meta(&"_harness_username")
	var payload := make_join_payload(username)
	var target := JoinTarget.new()
	target.backend = client.backend
	target.address = "localhost"
	var join_err: Error = await client.join(target, payload)
	assert(
		join_err == OK,
		"Client reconnect failed: %s" % error_string(join_err)
	)

	var peer_id := client.multiplayer_peer.get_unique_id()
	var server_api := _server.multiplayer_api
	await _wait_until(
		func() -> bool: return peer_id in server_api.get_peers(),
		"server to register reconnected peer %d" % peer_id,
	)


#endregion

#region Player flows

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
	assert(
		s,
		"admit_client_to_scene: scene '%s' not active on server." % scene_name
	)
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
func join_player(
	client: MultiplayerTree,
	level_scene_path: String,
	spawner_node_path: String,
) -> Node:
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

	var timed_out := await _wait_until(
		func() -> bool: return scene.level.get_node_or_null(player_path) != null,
		"player '%s' in scene '%s'" % [player_name, scene_name],
	)
	if timed_out:
		return null

	return scene.level.get_node_or_null(player_path)


## Builds a [JoinPayload] for harness-driven session entry.
##
## Leave [param level_scene_path] and [param spawner_node_path] empty for
## sceneless joins that should not spawn a player.
func make_join_payload(
	username: String,
	level_scene_path: String = "",
	spawner_node_path: String = "",
) -> JoinPayload:
	var join_payload := JoinPayload.new()
	join_payload.username = username
	if not level_scene_path.is_empty() and not spawner_node_path.is_empty():
		var spawner_component_path := SceneNodePath.new()
		spawner_component_path.scene_path = level_scene_path
		spawner_component_path.node_path = spawner_node_path
		join_payload.spawner_component_path = spawner_component_path
	return join_payload


## Creates a standalone listen-server tree and connects its local player.
func add_listen_server(
	join_payload: JoinPayload,
	auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := _create_player_tree("HarnessListenServer")
	tree.use_listen_server = true
	tree.auth_provider = auth_provider
	var target := JoinTarget.new()
	target.backend = tree.backend
	target.address = tree.backend.get_join_address()
	var err: Error = await tree.join_or_host(target, join_payload)
	assert(
		err == OK,
		"listen-server join_or_host() failed: %s" % error_string(err)
	)
	return tree


## Creates a client tree that joins the harness server via
## [method MultiplayerTree.auto_connect_player].
func add_connect_player(
	join_payload: JoinPayload,
	auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := await create_connect_player_tree(
		"HarnessConnectPlayer",
		auth_provider
	)
	var target := JoinTarget.new()
	target.backend = tree.backend
	target.address = tree.backend.get_join_address()
	var err: Error = await tree.join_or_host(target, join_payload)
	assert(
		err == OK,
		"join_or_host() failed: %s" % error_string(err)
	)
	return tree


## Creates a client tree wired to the harness server without connecting it.
func create_connect_player_tree(
	tree_name: String = "HarnessConnectPlayer",
	auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	await _ensure_server_hosted()
	var tree := _create_player_tree(tree_name, _session)
	tree.auth_provider = auth_provider
	return tree


## Creates a standalone player tree and drives
## [method MultiplayerTree.host_player].
func add_host_player(
	join_payload: JoinPayload,
	auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := _create_player_tree("HarnessHostPlayer")
	tree.auth_provider = auth_provider
	var err: Error = await tree.host_player(join_payload)
	assert(err == OK, "host_player() failed: %s" % error_string(err))
	return tree


## Registers a programmatically built [PackedScene] in the harness.
##
## Asserts that the harness session is a [LocalLoopbackSession]. Mirrors the
## registration onto all currently connected clients.
func register_built_scene(packed: PackedScene) -> void:
	assert(
		_session != null,
		"register_built_scene: Harness must be set up."
	)
	var path := packed.resource_path
	assert(
		not path.is_empty(),
		"register_built_scene: PackedScene must have a valid resource path."
	)
	var server_sm := server_scene_manager()
	if server_sm:
		server_sm.add_spawnable_scene(path)
	for client in _clients:
		var client_sm := scene_manager_for(client)
		if client_sm:
			client_sm.add_spawnable_scene(path)


## Spawns a player into a server scene, bypassing the RPC chain.
##
## Accepts [param scene_or_builder] which can be a [PackedScene], a live [Node],
## or a builder implementing [method build]. Returns the spawned player node.
func spawn_player(
	client: MultiplayerTree,
	scene_or_builder: Variant,
	scene_name: StringName = "",
) -> Node:
	var peer_id := client.multiplayer_peer.get_unique_id()
	var username: String = client.get_meta(&"_harness_username")
	var player: Node
	if scene_or_builder is PackedScene:
		player = (scene_or_builder as PackedScene).instantiate()
	elif scene_or_builder is Node:
		player = scene_or_builder as Node
	elif scene_or_builder.has_method("build"):
		player = scene_or_builder.build() as Node
	else:
		assert(
			false,
			"spawn_player: expected PackedScene, Node, or builder."
		)
		return null
	NetwEntity.bundle(player, peer_id, StringName(username))
	var scene := scene_on_server(scene_name)
	scene.add_player(player)
	return player


## Returns the server-side player node name for [param client].
func player_name_for(client: MultiplayerTree) -> StringName:
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	return NetwEntity.format_name(username, peer_id)


#endregion

#region Scene configuration

## Registers [param scene] as a spawnable scene on the server.
##
## Accepts a [PackedScene] or a [code]res://[/code] path. The path is
## mirrored onto every client created by subsequent [method add_client]
## calls. Must be called between [method setup] and the first
## [method add_client] so the registration reaches every client.
func register_spawnable_scene(scene: Variant) -> void:
	var path: String
	if scene is PackedScene:
		path = (scene as PackedScene).resource_path
	elif scene is String:
		path = scene
	else:
		assert(
			false,
			"register_spawnable_scene: expected PackedScene or String."
		)
		return

	var sm := server_scene_manager()
	assert(sm, "register_spawnable_scene: server has no MultiplayerSceneManager.")
	sm.add_spawnable_scene(path)


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
	var sm := server_scene_manager()
	assert(sm, "set_scene_policy: server has no MultiplayerSceneManager.")
	sm.set_scene_lifecycle_policy(scene_name, load_mode, empty_action)


#endregion

#region Scene waits

## Returns the named active scene from the server's scene manager, or the
## first active scene if [param scene_name] is empty.
func scene_on_server(scene_name: StringName = "") -> MultiplayerScene:
	var server_sm := server_scene_manager()
	if scene_name.is_empty():
		return server_sm.active_scenes.values()[0]
	return server_sm.active_scenes.get(scene_name)


## Waits for [param scene_name] to become active on [param client]'s
## scene manager.
func wait_for_scene(
	client: MultiplayerTree,
	scene_name: StringName,
) -> MultiplayerScene:
	var sm := scene_manager_for(client)
	var timed_out := await _wait_until(
		func() -> bool: return sm.active_scenes.has(scene_name),
		"scene '%s' on client" % scene_name,
	)
	if timed_out:
		return null
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

	var find_player := func() -> Node:
		if player_name.is_empty():
			var players := scene.player_nodes()
			return players[0] if players.size() > 0 else null
		return _find_scene_player(scene, player_name)

	if find_player.call() != null:
		return find_player.call()

	var label := (
		"player in scene '%s'" % scene_name if player_name.is_empty()
		else "player '%s' in scene '%s'" % [player_name, scene_name]
	)
	var timed_out := await _wait_until(
		func() -> bool: return find_player.call() != null,
		label,
	)
	if timed_out:
		return null
	return find_player.call()


#endregion

#region Internals

# Routes a predicate wait through [member awaiter] so timeouts surface as
# clean framework failures instead of runtime asserts. Returns
# [code]true[/code] on timeout, [code]false[/code] on success.
func _wait_until(
	cond: Callable,
	label: String,
	timeout: float = DEFAULT_TIMEOUT,
) -> bool:
	if cond.call():
		return false
	_wait_generation += 1
	_poll_until(cond, _wait_generation)
	var timed_out: bool = await awaiter.call(_wait_satisfied, timeout, label)
	if timed_out:
		# Invalidates the poll loop so it does not emit late.
		_wait_generation += 1
	return timed_out


func _poll_until(cond: Callable, generation: int) -> void:
	while is_inside_tree() and generation == _wait_generation:
		await get_tree().process_frame
		if cond.call():
			_wait_satisfied.emit()
			return


func _ensure_server_hosted() -> void:
	if _server.is_online():
		return
	var host_err: Error = await _server.host()
	assert(host_err == OK, "Server host() failed: %s" % error_string(host_err))


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
	var server_sm := server_scene_manager()
	if not server_sm:
		return
	var paths: Array[String] = []
	for path: String in server_sm.scene_paths:
		if not paths.has(path):
			paths.append(path)
	for path: String in server_sm.get_configured_paths():
		if not paths.has(path):
			paths.append(path)
	for path: String in paths:
		sm.add_spawnable_scene(path)


func _create_player_tree(
	tree_name: String,
	session: LocalLoopbackSession = null,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = tree_name
	tree.auto_host_headless = false

	if _world_scene:
		tree.add_child(_world_scene.instantiate())

	add_child(tree)

	var backend := LocalLoopbackBackend.new()
	backend.session = session if session else LocalLoopbackSession.new()
	if not session:
		_extra_sessions.append(backend.session)
	tree.backend = backend

	if _scene_manager_src:
		var sm := _instantiate_scene_manager()
		if sm:
			_configure_client_scene_manager(sm)
			tree.add_child(sm)

	if _clock_enabled:
		_add_clock_node(tree)

	return tree


func _ensure_clock(mt: MultiplayerTree) -> NetworkClock:
	var existing := mt.get_service(NetworkClock) as NetworkClock
	if existing:
		return existing
	return _add_clock_node(mt)


func _add_clock_node(mt: MultiplayerTree) -> NetworkClock:
	var clock := NetworkClock.new()
	clock.name = "NetworkClock"
	clock.tickrate = _clock_tickrate
	clock.display_offset = _clock_display_offset
	mt.add_child(clock)
	return clock


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

#endregion
