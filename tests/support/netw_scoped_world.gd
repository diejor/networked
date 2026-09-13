## [NetwEmbeddingWorld] over a subpath [MultiplayerTree], the scoped topology.
##
## The host is a [MultiplayerTree] mounted under the case's own node, brought
## online by preparing its local player and assigning a [LocalLoopbackSession]
## server peer. Clients are sibling [MultiplayerTree]s that prepare and assign a
## client peer on the same bus. Every mount polls itself from
## [method Node._process], so
## [method pump_until] only has to yield frames.
##
## This is the same session the whole scoped suite runs on, cast as one
## conformance provider so its facts can be compared against [NetwRootWorld].
class_name NetwScopedWorld
extends NetwEmbeddingWorld

var _anchor: Node
var _server_tree: MultiplayerTree
var _client_trees: Array[MultiplayerTree] = []
var _host_api: NetwMultiplayer
var _initial_scene: PackedScene


func _init(anchor: Node) -> void:
	_anchor = anchor


func provider() -> String:
	return "scoped"


func declare_initial_scene(scene: PackedScene) -> void:
	_initial_scene = scene


func host() -> NetwMultiplayer:
	if _host_api != null:
		return _host_api
	_server_tree = _make_tree(&"ScopedWorldServer")
	_server_tree.desired_role = NetwMultiplayer.ROLE_LISTEN_SERVER
	_anchor.add_child(_server_tree)

	var loopback := LocalLoopbackSession.get_shared_session()
	if not loopback.has_live_server():
		loopback.reset()
	_server_tree.api.session_prepare_join(&"host", [])
	_server_tree.api.multiplayer_peer = loopback.get_server_peer()

	var api := _server_tree.api
	await pump_until(func() -> bool: return api.is_online)
	_host_api = api
	if _initial_scene != null:
		var level := _initial_scene.instantiate()
		api.entity_replicate(level)
		api.root.add_child(level)
	return _host_api


func mount_clock() -> void:
	Netw.configure_clock(_server_tree)


func add_client(username: String) -> NetwMultiplayer:
	await host()
	var tree := _make_tree(
		StringName("ScopedWorldClient%d" % _client_trees.size()),
	)
	tree.desired_role = NetwMultiplayer.ROLE_CLIENT
	_anchor.add_child(tree)
	_client_trees.append(tree)

	tree.api.session_prepare_join(StringName(username), [])
	tree.api.multiplayer_peer = LocalLoopbackSession \
			.get_shared_session() \
			.create_client_peer()

	var api := tree.api
	await pump_until(func() -> bool: return api.local_participant != null)
	return api


func pump_until(cond: Callable, timeout_ms: int = 3000) -> bool:
	var tree := _anchor.get_tree()
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await tree.process_frame
	return cond.call()


func dispose() -> void:
	for tree in _client_trees:
		if is_instance_valid(tree):
			tree.queue_free()
	_client_trees.clear()
	if is_instance_valid(_server_tree):
		_server_tree.queue_free()
	_server_tree = null
	_host_api = null


func _make_tree(node_name: StringName) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = node_name
	tree.auto_host_headless = false
	tree.peer_class = &"LocalMultiplayerPeer"
	return tree
