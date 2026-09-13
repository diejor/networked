## Static helpers for ENet-based integration tests.
##
## This helper covers integration tests that need real UDP
## sockets to exercise transport-specific behavior: the auth-phase
## handshake behind [method NetwConnectHandle.endpoint_probe], ENet-level
## disconnect/reconnect semantics, and so on.
##
## Every session here comes up the ordinary Godot way: the caller builds an
## [ENetMultiplayerPeer], prepares a local player when it wants one, and
## assigns the peer to the session.
## [codeblock]
## var host := await EnetTestSupport.start_host(self)
## var client := EnetTestSupport.make_client_tree(self, host.port)
## var err := await EnetTestSupport.join_client(client, host.port, &"player")
## [/codeblock]
class_name EnetTestSupport
extends RefCounted

const _PORT_RANGE_START := 30000
const _PORT_RANGE_SIZE := 100

## How long [method join_client] and [method await_online] wait, in
## milliseconds. It is the test's own patience, not a session deadline.
const WAIT_TIMEOUT_MS := 5000


## Builds and hosts a fresh [MultiplayerTree] backed by ENet on the
## first available port in the test range.
##
## The peer is built with [method ENetMultiplayerPeer.create_server] and
## assigned, so the port scan reads the bind result directly. No local player
## is seated, which is what a probe target wants.
##
## [param parent] receives the tree as a child. [param info_provider] is
## optionally declared as the hosted session's probe reply through
## [method Netw.configure_server_info], scoped to the tree. [param auth_timeout]
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
		var peer := ENetMultiplayerPeer.new()
		if peer.create_server(candidate) != OK:
			continue

		var tree := MultiplayerTree.new()
		tree.name = "EnetHost_%d" % candidate
		tree.auto_host_headless = false
		tree.peer_class = &"ENetMultiplayerPeer"
		tree.transport_settings = { port = candidate }
		parent.add_child(tree)

		if info_provider.is_valid():
			Netw.configure_server_info(tree, info_provider)
		tree.api.multiplayer_peer = peer
		if tree.api.multiplayer_peer != peer:
			tree.queue_free()
			await parent.get_tree().process_frame
			continue
		if auth_timeout > 0.0:
			tree.api.inner.auth_timeout = auth_timeout
		return { tree = tree, port = candidate }

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
	tree.peer_class = &"ENetMultiplayerPeer"
	tree.transport_settings = { port = port }
	parent.add_child(tree)
	return tree


## Connects [param tree] to a localhost host on [param port] and waits until
## [param username] is seated.
##
## The local player is prepared before the peer is assigned, so the session
## holds its hello and submits exactly one request once the connection is
## admitted. Returns [constant @GlobalScope.OK] when
## [member NetwMultiplayer.local_participant] arrives, the assignment refusal
## when the peer was refused, and [constant @GlobalScope.ERR_TIMEOUT] when the
## wait ran out.
static func join_client(
		tree: MultiplayerTree,
		port: int,
		username: StringName,
		join_args: Array = [],
) -> Error:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client("127.0.0.1", port)
	if err != OK:
		return err
	tree.api.session_prepare_join(username, join_args)
	tree.api.multiplayer_peer = peer
	if tree.api.multiplayer_peer != peer:
		return ERR_CANT_CONNECT
	var api := tree.api
	return await await_until(
			tree,
			func() -> bool: return api.local_participant != null,
	)


## Waits until [param tree]'s session is online.
static func await_online(tree: MultiplayerTree) -> Error:
	var api := tree.api
	return await await_until(tree, func() -> bool: return api.is_online)


## Yields frames under [param node] until [param condition] holds.
##
## Returns [constant @GlobalScope.OK] once it does, or
## [constant @GlobalScope.ERR_TIMEOUT] after [constant WAIT_TIMEOUT_MS].
static func await_until(node: Node, condition: Callable) -> Error:
	var deadline := Time.get_ticks_msec() + WAIT_TIMEOUT_MS
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			return ERR_TIMEOUT
		await node.get_tree().process_frame
	return OK


## Builds [code]{ peer_class, address }[/code] fields for the endpoint at
## [param port] on localhost.
static func make_connect_endpoint(
		port: int,
) -> Dictionary:
	return {
		peer_class = &"ENetMultiplayerPeer",
		address = "127.0.0.1:%d" % port,
	}


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
