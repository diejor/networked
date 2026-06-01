## Static helpers for WebRTC session integration tests.
##
## [NetwTestHarness] is built around [LocalLoopbackBackend] and does not
## generalize to a real [WebRTCSession]. This helper mirrors [EnetTestSupport]
## for the complementary case. It hosts and joins real [MultiplayerTree]s over
## a [PairedWebRTCBackend], so the WebRTC handshake runs over loopback ICE with
## signaling shortcut in process. No trackers or sockets are touched.
## [codeblock]
## var host := await WebRTCTestSupport.start_host(self)
## var client := WebRTCTestSupport.make_client_tree(self)
## var target := WebRTCTestSupport.make_join_target(client, host.room)
## await client.join(target, payload)
## [/codeblock]
class_name WebRTCTestSupport
extends RefCounted


## Builds and hosts a [MultiplayerTree] backed by [PairedWebRTCBackend].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]),
## [code]backend[/code] (the host backend the tree duplicated), and
## [code]room[/code] (the generated room id clients join with).
static func start_host(parent: Node) -> Dictionary:
	var tree := MultiplayerTree.new()
	tree.name = "WebRTCHost"
	tree.auto_host_headless = false
	tree.backend = _make_backend()
	parent.add_child(tree)

	var err: Error = await tree.host(true)
	if err != OK:
		push_error("WebRTCTestSupport: host failed: %s" % error_string(err))
		tree.queue_free()
		return {}
	return { tree = tree, backend = tree.backend, room = tree.backend.get_join_address() }


## Builds an offline client [MultiplayerTree] wired with a paired WebRTC
## backend. The tree is added under [param parent] but has not connected.
static func make_client_tree(
	parent: Node, name_suffix: String = ""
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = "WebRTCClient%s" % name_suffix
	tree.auto_host_headless = false
	tree.backend = _make_backend()
	parent.add_child(tree)
	return tree


## Builds a [JoinTarget] pointing [param client] at [param room].
static func make_join_target(client: MultiplayerTree, room: String) -> JoinTarget:
	var target := JoinTarget.new()
	target.backend = client.backend
	target.address = room
	return target


## Tears down [param tree] and drains the SceneTree so its peer is released
## before the next test begins.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	tree.queue_free()
	if scene_tree:
		for i in 3:
			await scene_tree.process_frame


# Offline ice_servers keep the loopback handshake from reaching the network.
static func _make_backend() -> PairedWebRTCBackend:
	var backend := PairedWebRTCBackend.new()
	backend.ice_servers = []
	return backend
