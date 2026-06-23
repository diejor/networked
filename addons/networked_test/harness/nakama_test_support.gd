## Static helpers for live Nakama integration tests.
##
## [NakamaTestSupport] mirrors [WebRTCTestSupport] for real relay sessions. It
## wires [MultiplayerTree], [NakamaLobbyDirectory], and [NakamaBackend] directly
## so tests own raw trees and frame polling.
## [codeblock]
## var host := await NakamaTestSupport.start_host(self)
## var client := NakamaTestSupport.make_client_tree(self, "client")
## var target := NakamaTestSupport.make_join_target(client, host.room)
## await client.join(target, NakamaTestSupport.payload("client"))
## [/codeblock]
class_name NakamaTestSupport
extends RefCounted

const _RUN_PREFIX_ENV := "NETW_NAKAMA_TEST_RUN"

static var _prefix := ""


## Builds and hosts a [MultiplayerTree] backed by [NakamaBackend].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]) and
## [code]room[/code] (the Nakama relay match id).
static func start_host(
		parent: Node,
		username: String = "host",
) -> Dictionary:
	var tree := _make_tree(parent, "NakamaHost", username)
	var err: Error = await tree.host_player(payload(username))
	if err != OK:
		push_error("NakamaTestSupport: host failed: %s" % error_string(err))
		tree.queue_free()
		return { }
	return {
		tree = tree,
		room = tree.backend.get_join_address(),
	}


## Builds an offline client [MultiplayerTree] wired for Nakama.
static func make_client_tree(
		parent: Node,
		username: String,
) -> MultiplayerTree:
	return _make_tree(parent, "NakamaClient_%s" % username, username)


## Builds a [JoinTarget] pointing [param client] at [param room].
static func make_join_target(
		client: MultiplayerTree,
		room: String,
) -> JoinTarget:
	var target := JoinTarget.new()
	target.backend = client.backend
	target.address = room
	return target


## Tears down [param tree] and leaves the Nakama relay match.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	var dir := directory(tree)
	if dir != null:
		# leave_lobby is a coroutine when it deletes a hosted browse card, so
		# await it before freeing the tree or the storage delete races the
		# facade teardown.
		await dir.leave_lobby()
	tree.queue_free()
	if scene_tree:
		await NetwTestSuite.drain_frames(scene_tree, 5)


## Instantiates [param packed], wires its single [MultiplayerTree], and hosts.
static func host_scene(
		parent: Node,
		packed: PackedScene,
		username: String = "host",
) -> Dictionary:
	var scene := packed.instantiate()
	var tree := _find_tree(scene)
	_configure_tree(tree, username)
	parent.add_child(scene)

	var err: Error = await tree.host_player(payload(username, _level_1_spawn()))
	if err != OK:
		push_error("NakamaTestSupport: host scene failed: %s" % error_string(err))
		scene.queue_free()
		return { }
	return {
		tree = tree,
		room = tree.backend.get_join_address(),
		scene = scene,
	}


## Instantiates [param packed], wires its [MultiplayerTree], and joins.
static func join_scene(
		parent: Node,
		packed: PackedScene,
		room: String,
		username: String,
) -> MultiplayerTree:
	var scene := packed.instantiate()
	var tree := _find_tree(scene)
	_configure_tree(tree, username)
	parent.add_child(scene)

	var err: Error = await tree.join(
		make_join_target(tree, room),
		payload(username, _level_1_spawn()),
		10.0,
	)
	if err != OK:
		push_error("NakamaTestSupport: join scene failed: %s" % error_string(err))
	return tree


## Builds a [JoinPayload] for [param username].
static func payload(
		username: String,
		spawn: Dictionary = { },
) -> JoinPayload:
	var p := JoinPayload.new()
	p.username = StringName(username)
	p.spawn = spawn.duplicate(true)
	return p


static func _make_tree(
		parent: Node,
		tree_name: String,
		username: String,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = tree_name
	_configure_tree(tree, username)
	parent.add_child(tree)
	return tree


static func _configure_tree(tree: MultiplayerTree, username: String) -> void:
	tree.auto_host_headless = false
	tree.desired_role = MultiplayerTree.Role.LISTEN_SERVER
	_attach_directory(tree, username)
	tree.backend = NakamaBackend.new()


static func _attach_directory(tree: MultiplayerTree, username: String) -> void:
	var dir := directory(tree)
	if dir == null:
		dir = NakamaLobbyDirectory.new()
		dir.name = &"NakamaLobbyDirectory"
		tree.add_child(dir)
	dir.host = NakamaTestServer.host()
	dir.port = NakamaTestServer.DEFAULT_PORT
	dir.use_ssl = false
	dir.device_id = "%s-%s" % [_run_prefix(), username]
	dir.local_member_name = "%s-%s" % [_run_prefix(), username]


## Returns the [NakamaLobbyDirectory] attached to [param tree].
##
## [method MultiplayerTree.find_service_node] only matches scene-owned nodes, so
## a directory added with [code].new()[/code] falls back to a lookup by node name.
static func directory(tree: MultiplayerTree) -> NakamaLobbyDirectory:
	var dir := tree.find_service_node(NakamaLobbyDirectory) as NakamaLobbyDirectory
	if dir != null:
		return dir
	return tree.get_node_or_null("NakamaLobbyDirectory") as NakamaLobbyDirectory


static func _find_tree(scene: Node) -> MultiplayerTree:
	var found: Array[MultiplayerTree] = []
	for node in _collect_nodes(scene):
		if node is MultiplayerTree:
			found.append(node)
	assert(
		found.size() == 1,
		"NakamaTestSupport: expected one MultiplayerTree. Found %d."
		% found.size(),
	)
	return found[0]


static func _collect_nodes(root: Node) -> Array[Node]:
	var nodes: Array[Node] = [root]
	for child in root.get_children():
		nodes.append_array(_collect_nodes(child))
	return nodes


static func _level_1_spawn() -> Dictionary:
	var path := SceneNodePath.new(
		"uid://bqi7mvxdnvgch::Player/Components/MultiplayerEntity"
	)
	return EntitySpawnPolicy.from_scene_node_path(path).to_dict()


static func _run_prefix() -> String:
	if not _prefix.is_empty():
		return _prefix
	var env_prefix := OS.get_environment(_RUN_PREFIX_ENV)
	if not env_prefix.is_empty():
		_prefix = env_prefix
	else:
		_prefix = "netw-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	return _prefix
