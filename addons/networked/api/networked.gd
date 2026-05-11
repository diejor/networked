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
##
## [br][br]
## [b]Security note:[/b] [code]any_peer[/code] RPCs broadcast to every peer
## when called with [code].rpc()[/code]. Always use [code].rpc_id(1)[/code]
## for client-to-server requests and validate the sender inside the handler.
## [br][br]
## [b]Note:[/b] Always check [method NetwContext.is_valid] before caching a
## context reference. The underlying [MultiplayerTree] may be freed during
## disconnect or scene changes.
class_name Netw
extends Object


## Static entry point for all debug and logging functionality.
static var dbg: NetwDbg = NetwDbg.new()


## Returns a [NetwContext] for [param node] by walking its ancestor chain.
##
## Shorthand for [method NetwContext.for_node].
static func ctx(node: Node) -> NetwContext:
	return NetwContext.for_node(node)


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
