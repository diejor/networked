## Static helpers for live Nakama integration tests.
##
## [NakamaTestSupport] mirrors [WebRTCTestSupport] for real relay sessions. It
## wires [MultiplayerTree], [NakamaLobbyDirectory], and [NakamaBackend] directly
## so tests own raw trees and frame polling. Every tree here comes online
## through [NetwOrdinarySetup], so the relay peer is built by the provider and
## then assigned like any other, which is what makes delivery and adoption
## ordering observable.
## [codeblock]
## var host := await NakamaTestSupport.start_host(self)
## var client := NakamaTestSupport.make_client_tree(self, "client")
## var joined := await NakamaTestSupport.join_client(
##         client, host.room, "client")
## [/codeblock]
class_name NakamaTestSupport
extends RefCounted

## The [MultiplayerPeer] class Nakama relay matches join through.
const PEER_CLASS := &"NakamaRelayPeer"

const _RUN_PREFIX_ENV := "NETW_NAKAMA_TEST_RUN"

static var _prefix := ""

# Cached per PackedScene so a do_skip expression can call the check without
# instantiating the scene again on every evaluation.
static var _drivable := { }


## Returns why a suite driving [param packed] must skip, or an empty [String]
## when it can run.
##
## Answers both halves a live suite depends on, the reachable server and the
## drivable scene, so a suite states one condition instead of two.
## [codeblock]
## func before(
##         do_skip = NakamaTestSupport.skip_reason(MAIN) != "",
##         skip_reason = NakamaTestSupport.skip_reason(MAIN),
## ) -> void:
## [/codeblock]
static func skip_reason(packed: PackedScene) -> String:
	if NakamaTestServer.unavailable():
		return NakamaTestServer.SKIP_REASON
	return undrivable(packed)


## Returns why [param packed] cannot be driven, or an empty [String] when it can.
##
## [method host_scene] and [method join_scene] configure a scene-owned
## [MultiplayerTree] per peer, which is what lets one process stand up several
## independently configured peers. A scene that installs its session at the root
## instead has no such node, so this harness cannot drive it, and a suite that
## says so is honest where one that dies on the resulting [code]null[/code]
## reports four cascading errors for a single cause.
static func undrivable(packed: PackedScene) -> String:
	if _drivable.has(packed):
		return _drivable[packed]
	var scene := packed.instantiate()
	var found := 0
	for node in _collect_nodes(scene):
		if node is MultiplayerTree:
			found += 1
	scene.free()
	var reason := ""
	if found != 1:
		reason = (
				"%s needs one scene-owned MultiplayerTree per peer and this scene "
				+ "has %d. A root-installed session cannot be driven by this "
				+ "harness."
		) % [packed.resource_path, found]
	_drivable[packed] = reason
	return reason


## Builds and hosts a [MultiplayerTree] backed by [NakamaBackend].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]) and
## [code]room[/code] (the Nakama relay match id).
static func start_host(
		parent: Node,
		username: String = "host",
) -> Dictionary:
	var tree := _make_tree(parent, "NakamaHost", username)
	var settled := await NetwOrdinarySetup.connect_through(
			tree,
			NetwMultiplayer.TRANSPORT_MODE_HOST,
			"",
			{ },
			StringName(username),
	)
	if settled.error != OK:
		push_error(
			"NakamaTestSupport: host failed: %s %s" % [
				error_string(settled.error),
				settled.detail,
			],
		)
		tree.queue_free()
		return { }
	return {
		tree = tree,
		room = Netw.connection(tree).join_address,
	}


## Joins [param client] to [param room] as [param username].
##
## Returns [constant @GlobalScope.OK] once the relay peer is built and
## assigned, and the provider's own error otherwise.
static func join_client(
		client: MultiplayerTree,
		room: String,
		username: String,
		join_args: Array = [],
) -> Error:
	var settled := await NetwOrdinarySetup.connect_through(
			client,
			NetwMultiplayer.TRANSPORT_MODE_CLIENT,
			room,
			{ },
			StringName(username),
			join_args,
	)
	return settled.error


## Builds an offline client [MultiplayerTree] wired for Nakama.
static func make_client_tree(
		parent: Node,
		username: String,
) -> MultiplayerTree:
	return _make_tree(parent, "NakamaClient_%s" % username, username)


## Tears down [param tree] and leaves the Nakama relay match.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	var dir := directory(tree)
	if dir != null:
		# _leave_lobby is a coroutine when it deletes a hosted browse card, so
		# await it before freeing the tree or the storage delete races the
		# facade teardown.
		await dir._leave_lobby()
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

	var settled := await NetwOrdinarySetup.connect_through(
			tree,
			NetwMultiplayer.TRANSPORT_MODE_HOST,
			"",
			{ },
			StringName(username),
			_level_1_spawn(),
	)
	if settled.error != OK:
		push_error(
			"NakamaTestSupport: host scene failed: %s %s" % [
				error_string(settled.error),
				settled.detail,
			],
		)
		scene.queue_free()
		return { }
	return {
		tree = tree,
		room = Netw.connection(tree).join_address,
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

	var err := await join_client(tree, room, username, _level_1_spawn())
	if err != OK:
		push_error("NakamaTestSupport: join scene failed: %s" % error_string(err))
	return tree


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
	tree.desired_role = NetwMultiplayer.ROLE_LISTEN_SERVER
	_attach_directory(tree, username)
	tree.peer_class = PEER_CLASS


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
## The descendant scan only matches scene-owned nodes, so a directory added
## with [code].new()[/code] falls back to a lookup by node name.
static func directory(tree: MultiplayerTree) -> NakamaLobbyDirectory:
	for child in tree.find_children("*", "NakamaLobbyDirectory", true):
		return child as NakamaLobbyDirectory
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


static func _level_1_spawn() -> Array:
	return [&"Level1", NodePath("Player")]


static func _run_prefix() -> String:
	if not _prefix.is_empty():
		return _prefix
	var env_prefix := OS.get_environment(_RUN_PREFIX_ENV)
	if not env_prefix.is_empty():
		_prefix = env_prefix
	else:
		_prefix = "netw-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	return _prefix
