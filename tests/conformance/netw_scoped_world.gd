## [NetwEmbeddingWorld] over a subpath [MultiplayerTree], the harness topology.
##
## Wraps a [NetwTestHarness]: the host is the server tree, clients are subpath
## client trees, and every mount is polled by its own [Node._process] over the
## shared [LocalLoopbackSession]. This is the topology the whole existing suite
## runs on, cast as one conformance provider so its facts can be compared against
## [NetwRootWorld].
class_name NetwScopedWorld
extends NetwEmbeddingWorld

var _harness: NetwTestHarness
var _host_api: NetwMultiplayer
var _initial_scene: PackedScene


func _init(harness: NetwTestHarness) -> void:
	_harness = harness


func provider() -> String:
	return "scoped"


# The scoped-native declaration path: a MultiplayerSceneManager child under the
# server tree registers the scene before the tree hosts.
func declare_initial_scene(scene: PackedScene) -> void:
	_initial_scene = scene


func host() -> NetwMultiplayer:
	if _host_api != null:
		return _host_api
	if _initial_scene != null:
		await _harness.setup_factory(NetwTestSuite.create_scene_manager)
		_harness.register_spawnable_scene(_initial_scene)
	else:
		await _harness.setup()
	await _harness.host_server()
	_host_api = _harness.server().api
	return _host_api


func mount_clock() -> void:
	var clock := MultiplayerClock.new()
	clock.name = &"ConformanceClock"
	_harness.server().add_child(clock)


func add_client(username: String) -> NetwMultiplayer:
	await host()
	var client := await _harness.add_client(username)
	var api := client.api
	await pump_until(func() -> bool: return api.local_participant != null)
	return api


func pump_until(cond: Callable, timeout_ms: int = 3000) -> bool:
	var tree := _harness.get_tree()
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		await tree.process_frame
	return cond.call()


func dispose() -> void:
	# The suite auto-tears the managed harness down; nothing global to restore.
	pass
