@tool
## Entry point for the Networked addon.
##
## Use [method ctx] to obtain a [NetwContext] from any node inside a
## multiplayer session. The context gives you access to three sub-facades:
## [br]- [member NetwContext.tree] -- network operations (pause, kick,
##   disconnect). See [NetwTree].
## [br]- [member NetwContext.services] -- backend systems such as the
##   [NetworkClock] and [MultiplayerSceneManager], plus custom services
##   you register yourself. See [NetwServices].
## [br]- [member NetwContext.scene] -- lobby logic (readiness gates,
##   countdowns, player waiting). See [NetwScene].
##
## [br][br]
## [b]Other Entry Points[/b]
##
## [br]- [member dbg] -- structured logging and causal tracing. See
##   [NetwDbg].
## [br]- [member spawn] -- player spawn primitives. See [NetwSpawn].
##
## [br][br]
## [b]Listen-Server and Custom RPCs[/b]
##
## When writing custom [code]@rpc[/code] functions, use [method NetwTree.is_listen_server]
## as the single source of truth for listen-server checks.
## [br][br]
## [b]Request RPCs (client -> server):[/b]
## [codeblock]
## var ctx := Netw.ctx(self)
## if ctx.tree.is_listen_server():
##     _rpc_handle_input(data)
## else:
##     _rpc_handle_input.rpc_id(1, data)
## [/codeblock]
## You need this guard because [code]call_local[/code] does [b]not[/b] solve the
## problem for request RPCs: [method MultiplayerAPI.get_remote_sender_id]
## returns [code]0[/code] for locally-executed RPCs, breaking server-side
## sender validation. Use [method sender_id] in handlers to resolve the
## [code]0[/code] sender gotcha.
## [br][br]
## [b]Broadcast RPCs (server -> clients):[/b]
## Loop over [method NetwScene.get_peers] and call [code]rpc_id(peer_id)[/code].
## The internal facades ([method NetwTree.pause], [method NetwScene.suspend],
## etc.) already handle listen-server logic; prefer them over manual RPCs.
## [br][br]
## [b]Security note:[/b] [code]any_peer[/code] + [code]call_remote[/code] RPCs
## broadcast to every peer when called with [code].rpc()[/code]. Always use
## [code].rpc_id(1)[/code] for client-to-server requests and validate the sender
## inside the handler.
## [br][br]
## [b]Note:[/b] Always check [method NetwContext.is_valid] before caching a
## context reference. The underlying [MultiplayerTree] may be freed during
## disconnect or scene changes.
class_name Netw
extends Object


## Static entry point for all debug and logging functionality.
static var dbg := NetwDbg.new()


## Static entry point for player spawn helpers.
static var spawn := NetwSpawn.new()


## Returns a [NetwContext] for [param node] by walking its ancestor chain.
##
## Shorthand for [method NetwContext.for_node].
static func ctx(node: Node) -> NetwContext:
	return NetwContext.for_node(node)


## Returns the peer ID of the RPC sender for [param node].
##
## Treats local execution (where [method MultiplayerAPI.get_remote_sender_id]
## returns [code]0[/code]) as the local peer ID. Use this in custom [code]@rpc[/code]
## handlers to avoid the [code]0[/code] sender gotcha on listen servers.
## [codeblock]
## @rpc("any_peer", "call_local", "reliable")
## func _rpc_handle(data):
##     var peer_id := Netw.sender_id(self)
##     # peer_id is always a valid peer ID, never 0
## [/codeblock]
static func sender_id(node: Node) -> int:
	var id := node.multiplayer.get_remote_sender_id()
	return id if id != 0 else node.multiplayer.get_unique_id()


## Returns [code]true[/code] if the current process is running under a GdUnit4
## test environment.
static func is_test_env() -> bool:
	if Engine.has_meta("GdUnitRunner"):
		return true
	var args := OS.get_cmdline_args() + OS.get_cmdline_user_args()
	for arg in args:
		if "GdUnit" in arg or "GdUnitTestRunner.tscn" in arg:
			return true
	return false
