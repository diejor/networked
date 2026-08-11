## Static helpers for ENet-based integration tests.
##
## This helper covers integration tests that need real UDP
## sockets to exercise transport-specific behavior: the auth-phase
## handshake behind [method NetwConnector.probe], ENet-level
## disconnect/reconnect semantics, and so on.
class_name EnetTestSupport
extends RefCounted

const _PORT_RANGE_START := 30000
const _PORT_RANGE_SIZE := 100


## Builds and hosts a fresh [MultiplayerTree] backed by ENet on the
## first available port in the test range.
##
## [param parent] receives the tree as a child. [param info_provider] is
## optionally installed as the hosted session's per-session probe reply through
## [method NetwSessionHandle.set_server_info_provider]. [param auth_timeout]
## overrides the host API auth cleanup timeout when greater than [code]0.0[/code].
##
## Returns a dictionary with [code]tree[/code] (the [MultiplayerTree]),
## [code]port[/code] (the bound UDP port).
static func start_host(
		parent: Node,
		info_provider: Callable = Callable(),
		auth_timeout: float = -1.0,
) -> Dictionary:
	var port_range_end := _PORT_RANGE_START + _PORT_RANGE_SIZE
	for candidate in range(_PORT_RANGE_START, port_range_end):
		var tree := MultiplayerTree.new()
		tree.name = "EnetHost_%d" % candidate
		tree.auto_host_headless = false
		var params := NetwENetParams.new()
		params.port = candidate
		tree.transport = params
		parent.add_child(tree)

		var err := NetwConnector.error_of(
			await NetwConnector.of(tree.api).host(null),
		)
		if err == OK:
			if info_provider.is_valid():
				Netw.of(tree).session.set_server_info_provider(info_provider)
			if auth_timeout > 0.0:
				tree.api.inner.auth_timeout = auth_timeout
			return { tree = tree, port = candidate }

		tree.queue_free()
		await parent.get_tree().process_frame

	push_error(
		"EnetTestSupport: could not bind any port in [%d, %d)" % [
			_PORT_RANGE_START,
			_PORT_RANGE_START + _PORT_RANGE_SIZE,
		],
	)
	return { }


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
	var params := NetwENetParams.new()
	params.port = port
	tree.transport = params
	parent.add_child(tree)
	return tree


## Builds a [NetwConnectTarget] targeting [param port] on localhost.
static func make_connect_target(
		port: int,
) -> NetwConnectTarget:
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"
	target.metadata = { "port": port }
	return target


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
