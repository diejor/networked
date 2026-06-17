## Test rig for one in process multiplayer session.
##
## [member server], [method add_client], [method join_player], and
## [method teardown] keep tests on the public session surface while
## [LocalLoopbackSession] carries packets inside one [SceneTree].
## [codeblock]
## var harness := make_harness()
## await harness.setup(NetwTestSuite.create_scene_manager)
## harness.register_spawnable_scene(LEVEL)
## var client := await harness.add_client()
## var player := await harness.join_player(
##     client,
##     "uid://...",
##     "Player/Components/MultiplayerEntity"
## )
## await harness.teardown()
## [/codeblock]
class_name NetwTestHarness
extends Node

const DEFAULT_TIMEOUT := 1.0

## Reports harness wait timeouts.
##
## GdUnit factories install a reporter that records assertion failures.
## Plain Godot callers may leave the default [code]push_error[/code]
## reporter or assign their own.
var reporter: Callable = _default_reporter

var _session: LocalLoopbackSession
var _loopback: NetwHarnessSession
var _server: MultiplayerTree
var _clients: Array[MultiplayerTree] = []
var _scene_manager_factory: Callable = Callable()
var _scene_manager_scene: PackedScene = null
var _world_scene: PackedScene
var _waiter: NetwWaiter
var _extra_sessions: Array[LocalLoopbackSession] = []
var _clock_enabled: bool = false
var _clock_tickrate: int = 30
var _clock_display_offset: int = 3
var _torn_down := false

#region Lifecycle

## Creates the [LocalLoopbackSession] and server [MultiplayerTree].
##
## Does not host yet. Register spawnable scenes with
## [method register_spawnable_scene] before calling [method add_client].
## [codeblock]
## await harness.setup(NetwTestSuite.create_scene_manager)
## harness.register_spawnable_scene(LEVEL)
## var client := await harness.add_client()
## [/codeblock]
func setup(world_scene: PackedScene = null) -> void:
	_setup_session(world_scene)
	await get_tree().process_frame


func setup_scene(scene: PackedScene, world_scene: PackedScene = null) -> void:
	_scene_manager_scene = scene
	_setup_session(world_scene)
	await get_tree().process_frame


func setup_factory(factory: Callable, world_scene: PackedScene = null) -> void:
	_scene_manager_factory = factory
	_setup_session(world_scene)
	await get_tree().process_frame


func _setup_session(world_scene: PackedScene = null) -> void:
	_world_scene = world_scene
	_loopback = NetwHarnessSession.new()
	_session = _loopback.session()
	_waiter = NetwWaiter.new(get_tree(), reporter)
	_setup_server()


## Frees harness peers and resets loopback sessions.
##
## [NetwTestSuite] calls this automatically for harnesses created through
## [method NetwTestSuite.make_harness]. Direct users may call it explicitly.
## Repeated calls are ignored.
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

	if _loopback:
		_loopback.reset()
	_session = null
	_loopback = null

	for extra_session in _extra_sessions:
		if extra_session:
			extra_session.reset()
	_extra_sessions.clear()
	_scene_manager_factory = Callable()
	_scene_manager_scene = null
	_world_scene = null
	_waiter = null
	reporter = Callable()

	if is_inside_tree():
		get_parent().remove_child(self)

	if tree:
		await NetwTestSuite.drain_frames(tree, 1)

	queue_free()

#endregion

#region Peers

## Creates and joins a client [MultiplayerTree].
##
## The first call hosts [method server] after
## [method register_spawnable_scene] has had a chance to configure scenes.
## [codeblock]
## var client := await harness.add_client()
## assert_bool(client.is_online()).is_true()
## [/codeblock]
func add_client(username: String = "") -> MultiplayerTree:
	await _ensure_server_hosted()

	var index := _clients.size()
	if username.is_empty():
		username = "test_player_%d" % index

	var client := _make_service_tree(
		MultiplayerTree.Role.CLIENT,
		"HarnessClient%d" % index,
	)
	client.set_meta(&"_harness_username", username)

	_clients.append(client)

	var payload := make_sceneless_payload(username)
	var join_err: Error = await _loopback.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		payload,
	)
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


## Creates a [MultiplayerClock] on [method server] and every client.
##
## Clients created after this call receive the same clock before joining.
## Existing clients are awaited until [signal MultiplayerClock.clock_synchronized]
## fires.
func add_clock(
		tickrate: int = 30,
		display_offset: int = 3,
) -> MultiplayerClock:
	_clock_enabled = true
	_clock_tickrate = tickrate
	_clock_display_offset = display_offset

	var server_clock := _ensure_clock(_server)
	for client in _clients:
		var client_clock := _ensure_clock(client)
		if not client_clock.is_synchronized:
			await _wait_until(
				func() -> bool: return client_clock.is_synchronized,
				"client clock synchronization",
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


## Degrades both network directions for [param client].
func degrade(client: MultiplayerTree) -> NetwLink.NetwLinkMulti:
	var inbound := path(_server, client)
	var outbound := path(client, _server)
	return NetwLink.NetwLinkMulti.new(inbound, outbound)


## Applies [param profile] to every connected client.
func degrade_clients(profile: NetwLink.Profile) -> void:
	for client in _clients:
		degrade(client).profile(profile)


## Clears all link simulation in this harness session.
func clear_links() -> void:
	_session.clear_all_link_conditions()


## Returns fluent path control for packets from [param from] to [param to].
func path(from: MultiplayerTree, to: MultiplayerTree) -> NetwLink:
	var peer := _loopback_peer_for(to, "path")
	var sender_id := from.multiplayer_peer.get_unique_id() if from and from.multiplayer_peer else 0
	return NetwLink.new(_session, peer, sender_id)


## Returns fluent inbound link control for [param client]'s loopback peer.
##
## Prefer [method degrade] or [method path]. This method preserves the old
## receiver keyed API used by existing tests.
func link(
		client: MultiplayerTree,
		from_peer_id: int = 0,
) -> NetwLink:
	var peer := _loopback_peer_for(client, "link")
	return NetwLink.new(_session, peer, from_peer_id)


## Disconnects [param client] without freeing it.
##
## The client can be passed to [method reconnect_client] afterward.
func disconnect_client(client: MultiplayerTree) -> void:
	assert(_clients.has(client), "disconnect_client: unknown client tree.")
	if not client.multiplayer_peer:
		return

	var peer_id := _loopback.disconnect_tree(client)
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
	var payload := make_sceneless_payload(username)
	var join_err: Error = await _loopback.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		payload,
	)
	assert(
		join_err == OK,
		"Client reconnect failed: %s" % error_string(join_err),
	)

	var peer_id := client.multiplayer_peer.get_unique_id()
	var server_api := _server.multiplayer_api
	await _wait_until(
		func() -> bool: return peer_id in server_api.get_peers(),
		"server to register reconnected peer %d" % peer_id,
	)

#endregion

#region Player flows

## Admits [param client] to [param scene_name] without spawning a player.
##
## Calls [method MultiplayerScene.connect_peer] on [method server] and waits
## for [param client] to activate the scene.
func admit_client_to_scene(
		client: MultiplayerTree,
		scene_name: StringName,
) -> MultiplayerScene:
	var s := scene_on_server(scene_name)
	assert(
		s,
		"admit_client_to_scene: scene '%s' not active on server." % scene_name,
	)
	var peer_id := client.multiplayer_peer.get_unique_id()
	s.connect_peer(peer_id)
	return await wait_for_scene(client, scene_name)


## Sends [method MultiplayerTree.request_join_player] from [param client].
##
## [param level_scene_path] must be registered with
## [method register_spawnable_scene]. [param spawner_node_path] is relative to
## that scene root.
## [codeblock]
## var player := await harness.join_player(
##     client,
##     LEVEL,
##     "Player/Components/MultiplayerEntity"
## )
## [/codeblock]
func join_player(
		client: MultiplayerTree,
		level_scene_path: String,
		spawner_node_path: String,
) -> Node:
	var username: String = client.get_meta(&"_harness_username")

	var entity_path := SceneNodePath.new()
	entity_path.scene_path = level_scene_path
	entity_path.node_path = spawner_node_path

	var join_payload := JoinPayload.new()
	join_payload.username = username
	join_payload.spawn = EntitySpawnPolicy.from_scene_node_path(
		entity_path,
	).to_dict()

	client.request_join_player.rpc_id(
		MultiplayerPeer.TARGET_PEER_SERVER,
		join_payload.serialize(),
	)

	var scene_name: StringName = entity_path.get_scene_name()
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


## Builds a [JoinPayload] that accepts a player without spawning a node.
func make_sceneless_payload(username: String) -> JoinPayload:
	return _loopback.build_join_payload(username)


## Builds a [JoinPayload] that spawns [param username] at
## [param spawner_node_path].
func make_spawn_payload(
		username: String,
		level_scene_path: String,
		spawner_node_path: String,
) -> JoinPayload:
	var entity_path := SceneNodePath.new()
	entity_path.scene_path = level_scene_path
	entity_path.node_path = spawner_node_path
	var payload := JoinPayload.new()
	payload.username = username
	payload.spawn = EntitySpawnPolicy.from_scene_node_path(entity_path).to_dict()
	return payload


## Builds a [JoinPayload] for harness driven session entry.
func make_join_payload(
		username: String,
		level_scene_path: String = "",
		spawner_node_path: String = "",
) -> JoinPayload:
	if not level_scene_path.is_empty() and not spawner_node_path.is_empty():
		return make_spawn_payload(username, level_scene_path, spawner_node_path)
	return make_sceneless_payload(username)


## Creates a standalone listen server and connects its local player.
func add_listen_server(
		join_payload: JoinPayload,
		auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := _make_service_tree(
		MultiplayerTree.Role.LISTEN_SERVER,
		"HarnessListenServer",
		false,
	)
	tree.auth_provider = auth_provider
	var err: Error = await _loopback.connect_tree(
		tree,
		NetwHarnessSession.Entry.JOIN_OR_HOST,
		join_payload,
	)
	assert(
		err == OK,
		"listen-server join_or_host() failed: %s" % error_string(err),
	)
	return tree


## Creates a client tree that joins the harness server through
## [method MultiplayerTree.join_or_host].
func add_connect_player(
		join_payload: JoinPayload,
		auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := await create_connect_player_tree(
		"HarnessConnectPlayer",
		auth_provider,
	)
	var err: Error = await _loopback.connect_tree(
		tree,
		NetwHarnessSession.Entry.JOIN_OR_HOST,
		join_payload,
	)
	assert(
		err == OK,
		"join_or_host() failed: %s" % error_string(err),
	)
	return tree


## Creates a client tree wired to the harness server without connecting it.
func create_connect_player_tree(
		tree_name: String = "HarnessConnectPlayer",
		auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	await _ensure_server_hosted()
	var tree := _make_service_tree(
		MultiplayerTree.Role.CLIENT,
		tree_name,
		true,
	)
	tree.auth_provider = auth_provider
	return tree


## Creates a standalone player host through
## [method MultiplayerTree.host_player].
func add_host_player(
		join_payload: JoinPayload,
		auth_provider: NetwAuthProvider = null,
) -> MultiplayerTree:
	var tree := _make_service_tree(
		MultiplayerTree.Role.CLIENT,
		"HarnessHostPlayer",
		false,
	)
	tree.auth_provider = auth_provider
	var err: Error = await _loopback.connect_tree(
		tree,
		NetwHarnessSession.Entry.HOST_PLAYER,
		join_payload,
	)
	assert(err == OK, "host_player() failed: %s" % error_string(err))
	return tree


## Registers a programmatically built [PackedScene] in the harness.
##
## Asserts that the harness session is a [LocalLoopbackSession]. Mirrors the
## registration onto all currently connected clients.
func register_built_scene(packed: PackedScene) -> void:
	assert(
		_session != null,
		"register_built_scene: Harness must be set up.",
	)
	var path := packed.resource_path
	assert(
		not path.is_empty(),
		"register_built_scene: PackedScene must have a valid resource path.",
	)
	var server_sm := server_scene_manager()
	if server_sm:
		server_sm.add_spawnable_scene(path)
	for client in _clients:
		var client_sm := scene_manager_for(client)
		if client_sm:
			client_sm.add_spawnable_scene(path)


## Spawns a player into a server scene without the join RPC.
##
## Accepts [param scene] as a [PackedScene]. Returns the spawned player node.
func spawn_player(
		client: MultiplayerTree,
		scene: PackedScene,
		scene_name: StringName = "",
) -> Node:
	var node := scene.instantiate()
	return spawn_player_node(client, node, scene_name)


func spawn_player_node(
		client: MultiplayerTree,
		node: Node,
		scene_name: StringName = "",
) -> Node:
	var peer_id := client.multiplayer_peer.get_unique_id()
	var username: String = client.get_meta(&"_harness_username")
	NetwEntity.bind(node, StringName(username), peer_id)
	var scene := scene_on_server(scene_name)
	scene.add_player(node)
	return node


func spawn_player_factory(
		client: MultiplayerTree,
		factory: Callable,
		scene_name: StringName = "",
) -> Node:
	var node := factory.call() as Node
	return spawn_player_node(client, node, scene_name)


## Returns the server side player node name for [param client].
func player_name_for(client: MultiplayerTree) -> StringName:
	var username: String = client.get_meta(&"_harness_username")
	var peer_id := client.multiplayer_peer.get_unique_id()
	var rj := ResolvedJoin.new()
	rj.username = StringName(username)
	rj.peer_id = peer_id
	return StringName(NetwEntity.name_for(rj))

#endregion

#region Scene configuration

## Registers [param scene] as spawnable on [method server].
##
## Accepts a [PackedScene] or a [code]res://[/code] path. The path is
## mirrored onto clients created by [method add_client].
## [codeblock]
## await harness.setup(NetwTestSuite.create_scene_manager)
## harness.register_spawnable_scene(LEVEL)
## var client := await harness.add_client()
## [/codeblock]
func register_spawnable_scene(scene: PackedScene) -> void:
	var path := scene.resource_path

	var sm := server_scene_manager()
	assert(sm, "register_spawnable_scene: server has no MultiplayerSceneManager.")
	sm.add_spawnable_scene(path)


## Configures the server lifecycle policy for [param scene_name].
##
## Call after [method setup] and before [method add_client] to apply it to
## every joined client.
## [codeblock]
## harness.set_scene_policy(
##     &"Arena",
##     MultiplayerSceneManager.LoadMode.ON_DEMAND,
##     MultiplayerSceneManager.EmptyAction.DESPAWN
## )
## [/codeblock]
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

## Returns an active [MultiplayerScene] from [method server_scene_manager].
##
## Empty [param scene_name] returns the first active scene.
func scene_on_server(scene_name: StringName = "") -> MultiplayerScene:
	var server_sm := server_scene_manager()
	if scene_name.is_empty():
		return server_sm.active_scenes.values()[0]
	return server_sm.active_scenes.get(scene_name)


## Waits for [param scene_name] to become active on [param client].
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

# Routes predicate waits through the shared waiter.
func _wait_until(
		cond: Callable,
		label: String,
		timeout: float = DEFAULT_TIMEOUT,
) -> bool:
	return await _waiter.until(cond, label, timeout)


func _loopback_peer_for(
		tree: MultiplayerTree,
		method_name: String,
) -> LocalMultiplayerPeer:
	assert(
		tree != null,
		"NetwTestHarness.%s: tree is required." % method_name,
	)
	var peer := tree.multiplayer_peer as LocalMultiplayerPeer
	assert(
		peer != null,
		(
				"NetwTestHarness.%s: link simulation requires "
				+ "LocalLoopbackBackend."
		) % method_name,
	)
	return peer


func _default_reporter(label: String, timeout: float) -> void:
	push_error("Timed out waiting for '%s' after %.2fs." % [label, timeout])


func _ensure_server_hosted() -> void:
	if _server.is_online():
		return
	var host_err: Error = await _loopback.connect_tree(
		_server,
		NetwHarnessSession.Entry.HOST,
	)
	assert(host_err == OK, "Server host() failed: %s" % error_string(host_err))


func _instantiate_scene_manager() -> MultiplayerSceneManager:
	if _scene_manager_scene != null:
		return _scene_manager_scene.instantiate()
	if not _scene_manager_factory.is_null():
		return _scene_manager_factory.call()
	return null


# Installs the default spawn policy unless the test supplied one.
func _ensure_default_spawn_policy(tree: MultiplayerTree) -> void:
	if tree.spawn_policy == null:
		tree.spawn_policy = EntitySpawnPolicy.new()


# Mirrors server scene config onto a new client manager.
func _configure_client_scene_manager(sm: MultiplayerSceneManager) -> void:
	var server_sm := server_scene_manager()
	if not server_sm:
		return
	var paths: Array[String] = []
	for path: String in server_sm.get_configured_paths():
		if not paths.has(path):
			paths.append(path)
	for path: String in paths:
		sm.add_spawnable_scene(path)


func _make_service_tree(
		role: MultiplayerTree.Role,
		tree_name: String,
		use_shared_session: bool = true,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = tree_name
	tree.desired_role = role

	if _world_scene:
		tree.add_child(_world_scene.instantiate())

	add_child(tree)

	if use_shared_session:
		_loopback.adopt_tree(tree, role)
	else:
		tree.auto_host_headless = false
		var backend := LocalLoopbackBackend.new()
		backend.session = LocalLoopbackSession.new()
		_extra_sessions.append(backend.session)
		tree.backend = backend

	if _scene_manager_scene != null or not _scene_manager_factory.is_null():
		var sm := _instantiate_scene_manager()
		if sm:
			if role != MultiplayerTree.Role.DEDICATED_SERVER:
				_configure_client_scene_manager(sm)
			tree.add_child(sm)
			_ensure_default_spawn_policy(tree)

	if _clock_enabled:
		_add_clock_node(tree)

	return tree


func _ensure_clock(mt: MultiplayerTree) -> MultiplayerClock:
	var existing := mt.get_service(MultiplayerClock) as MultiplayerClock
	if existing:
		return existing
	return _add_clock_node(mt)


func _add_clock_node(mt: MultiplayerTree) -> MultiplayerClock:
	var clock := MultiplayerClock.new()
	clock.name = "MultiplayerClock"
	clock.tickrate = _clock_tickrate
	clock.display_offset = _clock_display_offset
	mt.add_child(clock)
	return clock


func _setup_server() -> void:
	_server = _make_service_tree(
		MultiplayerTree.Role.DEDICATED_SERVER,
		"HarnessServer",
	)


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
