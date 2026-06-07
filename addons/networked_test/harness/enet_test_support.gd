## Static helpers for ENet-based integration tests.
##
## The helper embodies the [NetwHarnessSession.BackendAdapter] shape. It is
## kept static until a second ENet harness consumer needs an adapter instance.
## [br][br]
## [NetwTestHarness] is built around [LocalLoopbackBackend]: it gives cheap
## multi-tree-in-process gameplay tests, deterministic packet flow, and
## packet hold/release for race-condition tests. Those features rely on the
## in-process custom peer and do not generalize to real transports.
## [br][br]
## This helper covers the complementary case: tests that need real UDP
## sockets to exercise transport-specific behavior -- the auth-phase
## handshake behind [method BackendPeer.query_server_info], ENet-level
## disconnect/reconnect semantics, and so on. The two are not meant to
## compose; pick the one whose contract matches the unit under test.
class_name EnetTestSupport
extends RefCounted

const _PORT_RANGE_START := 30000
const _PORT_RANGE_SIZE := 100


## Builds and hosts a fresh [MultiplayerTree] backed by [ENetBackend] on the
## first available port in the test range.
##
## [param parent] receives the tree as a child. [param source] is optionally
## assigned to the tree's [member MultiplayerTree.server_info_source].
## [param auth_timeout] overrides the host API auth cleanup timeout when
## greater than [code]0.0[/code].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]),
## [code]port[/code] (the bound UDP port), and [code]backend[/code] (the
## host's [ENetBackend], duplicated by the tree's setter).
static func start_host(
		parent: Node,
		source: ServerInfoSource = null,
		auth_timeout: float = -1.0,
) -> Dictionary:
	var port_range_end := _PORT_RANGE_START + _PORT_RANGE_SIZE
	for candidate in range(_PORT_RANGE_START, port_range_end):
		var tree := MultiplayerTree.new()
		tree.name = "EnetHost_%d" % candidate
		tree.auto_host_headless = false
		tree.server_info_source = source

		var backend := ENetBackend.new()
		backend.port = candidate
		tree.backend = backend
		parent.add_child(tree)

		var err: Error = await tree.host(true)
		if err == OK:
			if auth_timeout > 0.0:
				tree.api.auth_timeout = auth_timeout
			return { tree = tree, port = candidate, backend = tree.backend }

		tree.queue_free()
		await parent.get_tree().process_frame

	push_error(
		"EnetTestSupport: could not bind any port in [%d, %d)" % [
			_PORT_RANGE_START,
			_PORT_RANGE_START + _PORT_RANGE_SIZE,
		],
	)
	return { }


## Builds a client-side [ENetBackend] configured to talk to [param port].
##
## Returned backend is not attached to any tree; pass it directly to
## [method BackendPeer.query_server_info] or to
## [method MultiplayerTree.join].
static func make_client_backend(port: int) -> ENetBackend:
	var backend := ENetBackend.new()
	backend.port = port
	return backend


## Builds an offline client [MultiplayerTree] wired with an ENet backend
## targeting [param port]. The tree is added under [param parent] but has not
## connected to anything.
static func make_client_tree(
		parent: Node,
		port: int,
		name_suffix: String = "",
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = "EnetClient%s" % name_suffix
	tree.auto_host_headless = false
	tree.backend = make_client_backend(port)
	parent.add_child(tree)
	return tree


## Tears down [param tree] and drains the SceneTree so the UDP socket is
## released before the next test begins.
static func stop_tree(tree: MultiplayerTree) -> void:
	if not is_instance_valid(tree):
		return
	var scene_tree := tree.get_tree()
	tree.queue_free()
	if scene_tree:
		for i in 3:
			await scene_tree.process_frame
