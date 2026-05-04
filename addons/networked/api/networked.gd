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
