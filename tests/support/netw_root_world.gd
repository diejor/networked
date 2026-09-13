## [NetwEmbeddingWorld] over a root-installed session, the shipping topology.
##
## The host is the [SceneTree]'s own default session with no
## owning [MultiplayerTree], brought online by preparing its local player and
## assigning a loopback server peer, the ordinary path. Clients are subpath
## [MultiplayerTree]s that prepare and assign a loopback client peer, joining
## across the mount boundary over the shared [LocalLoopbackSession] bus, the
## in-process shipping-host mode the analysis names (§4.2-1). The root host is not
## a node, so [method pump_until] pumps it by hand while [method SceneTree.process_frame]
## pumps the client trees. The root override is captured and restored in
## [method dispose] so it never leaks into the next case, the
## [TestRootOverrideInstall] discipline.
class_name NetwRootWorld
extends NetwEmbeddingWorld

const _DT := 1.0 / 60.0

var _host_node: Node
var _original: MultiplayerAPI
var _host_api: NetwMultiplayer
var _client_trees: Array[MultiplayerTree] = []
var _initial_scene: PackedScene
var _scene_decl: Node
var _initial_root_children: Dictionary[int, bool] = { }


func _init(host_node: Node) -> void:
	_host_node = host_node
	_original = host_node.get_tree().get_multiplayer()
	for child: Node in host_node.get_tree().root.get_children():
		_initial_root_children[child.get_instance_id()] = true


func provider() -> String:
	return "root"


func declare_initial_scene(scene: PackedScene) -> void:
	_initial_scene = scene


func host() -> NetwMultiplayer:
	if _host_api != null:
		return _host_api
	_host_api = NetwMultiplayer.make()
	_host_node.get_tree().set_multiplayer(_host_api)
	_host_api.embed_settle()
	_host_api.session_prepare_join(&"host", [])
	_host_api.multiplayer_peer = _loopback_server_peer()
	await pump_until(func() -> bool: return _host_api.is_online)
	if _initial_scene != null:
		_scene_decl = _initial_scene.instantiate()
		_host_api.entity_replicate(_scene_decl)
		_host_api.root.add_child(_scene_decl)
	return _host_api


func _loopback_server_peer() -> MultiplayerPeer:
	var loopback := LocalLoopbackSession.get_shared_session()
	if not loopback.has_live_server():
		loopback.reset()
	return loopback.get_server_peer()


func mount_clock() -> void:
	Netw.configure_clock(_host_node)


func add_client(username: String) -> NetwMultiplayer:
	await host()
	var tree := MultiplayerTree.new()
	tree.name = "RootWorldClient%d" % _client_trees.size()
	tree.desired_role = NetwMultiplayer.ROLE_CLIENT
	tree.auto_host_headless = false
	tree.peer_class = &"LocalMultiplayerPeer"
	_host_node.add_child(tree)
	_client_trees.append(tree)

	tree.api.session_prepare_join(StringName(username), [])
	tree.api.multiplayer_peer = LocalLoopbackSession \
			.get_shared_session() \
			.create_client_peer()

	var api := tree.api
	await pump_until(func() -> bool: return api.local_participant != null)
	return api


func pump_until(cond: Callable, timeout_ms: int = 3000) -> bool:
	var tree := _host_node.get_tree()
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		if _host_api != null:
			_host_api.poll()
			if _host_api.has_multiplayer_peer():
				_host_api.poll()
		await tree.process_frame
	return cond.call()


func dispose() -> void:
	for tree in _client_trees:
		if is_instance_valid(tree):
			tree.queue_free()
	_client_trees.clear()
	if is_instance_valid(_scene_decl):
		_scene_decl.queue_free()
	_scene_decl = null
	var tree := _host_node.get_tree()
	_queue_added_root_children(tree.root)
	if _host_api != null and tree.get_multiplayer() == _host_api:
		_host_api.multiplayer_peer = null
		_host_api.embed_dispose()
	_host_api = null
	tree.set_multiplayer(_original)


# Queues root-anchored content created by this provider.
func _queue_added_root_children(root: Window) -> void:
	for child: Node in root.get_children():
		if not _initial_root_children.has(child.get_instance_id()):
			child.queue_free()
