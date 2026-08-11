## [NetwEmbeddingWorld] over a root-installed session, the shipping topology.
##
## The host is a [method NetwMultiplayer.install_as_default] session with no
## owning [MultiplayerTree], brought online through its own
## [method NetwConnector.host] verb (the connect column re-home is what makes
## this reachable in-process). Clients are subpath [MultiplayerTree]s joining
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
var _clock: MultiplayerClock
var _initial_root_children: Dictionary[int, bool] = { }


func _init(host_node: Node) -> void:
	_host_node = host_node
	_original = host_node.get_tree().get_multiplayer()
	for child: Node in host_node.get_tree().root.get_children():
		_initial_root_children[child.get_instance_id()] = true


func provider() -> String:
	return "root"


# The root-native declaration path: a config added while the root session is
# offline, the clean declare-while-offline route install_as_default takes.
func declare_initial_scene(scene: PackedScene) -> void:
	_initial_scene = scene


func host() -> NetwMultiplayer:
	if _host_api != null:
		return _host_api
	_host_api = NetwMultiplayer.install_as_default(_host_node.get_tree())
	if _initial_scene != null:
		var config := NetwSceneConfig.new()
		config.isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD
		config.initial_scenes = [_initial_scene]
		_scene_decl = Node.new()
		_scene_decl.name = &"RootSceneDeclaration"
		_host_node.add_child(_scene_decl)
		_host_api.object_configuration_add(_scene_decl, config)
	# Faithful RootEmbedding: the autoload settles the session once after install,
	# so the declared scene resolves through the same phase edge the shipping path
	# uses (the config is registered synchronously above, so a direct call here
	# stands in for the autoload's one-shot process_frame).
	_host_api.embedding.settle()
	var payload := JoinPayload.new()
	payload.username = "host"
	var config := NetwHostConfig.new()
	config.transport = NetwLocalParams.new()
	await NetwConnector.of(_host_api).host(payload, config)
	await pump_until(func() -> bool: return _host_api.is_online)
	return _host_api


func mount_clock() -> void:
	_clock = MultiplayerClock.new()
	_clock.name = &"ConformanceClock"
	# Parented under the root-installed anchor, so the clock service resolves the
	# root session through its own multiplayer with no owning tree.
	_host_node.add_child(_clock)


func add_client(username: String) -> NetwMultiplayer:
	await host()
	var tree := MultiplayerTree.new()
	tree.name = "RootWorldClient%d" % _client_trees.size()
	tree.desired_role = NetwMultiplayer.Role.CLIENT
	tree.auto_host_headless = false
	tree.transport = NetwLocalParams.new()
	_host_node.add_child(tree)
	_client_trees.append(tree)

	var payload := JoinPayload.new()
	payload.username = username
	var target := NetwConnectTarget.new()
	target.scheme = &"local"
	NetwConnector.of(tree.api).join(target, payload)

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
	if is_instance_valid(_clock):
		_clock.queue_free()
	_clock = null
	var tree := _host_node.get_tree()
	_queue_added_root_children(tree.root)
	if _host_api != null and tree.get_multiplayer() == _host_api:
		_host_api.multiplayer_peer = null
		_host_api.embedding.dispose()
	_host_api = null
	tree.set_multiplayer(_original)


# Queues root-anchored content created by this provider.
func _queue_added_root_children(root: Window) -> void:
	for child: Node in root.get_children():
		if not _initial_root_children.has(child.get_instance_id()):
			child.queue_free()
