@tool
## Top-level namespace and entry point for the Networked addon.
##
## This is the primary API surface for gameplay code. All interactions with
## the networked session begin here.
##
## [br][br]
## [b]Quick Start[/b]
##
## Obtain a [NetwContext] from any node inside a multiplayer session:
## [codeblock]
## func _ready() -> void:
##     var ctx := Netw.ctx(self)
##     if not ctx.is_valid():
##         return
## [/codeblock]
## [br]
##
## The context exposes three sub-facades:
## [br]- [member NetwContext.tree]: session-level gameplay APIs
## [br]- [member NetwContext.services]: backend service locator
## [br]- [member NetwContext.scene]: scene-level lobby APIs
##
## [br][br]
## [b]Session APIs[/b]
##
## Use [member NetwContext.tree] for pause, kick, and session
## introspection:
## [codeblock]
## func _ready() -> void:
##     var ctx := Netw.ctx(self)
##     ctx.tree.tree_paused.connect(_on_paused)
##     if ctx.tree.is_server():
##         ctx.tree.pause("waiting for players")
## [/codeblock]
##
## [br][br]
## [b]Service Access[/b]
##
## Use [member NetwContext.services] to reach backend systems:
## [codeblock]
## func _ready() -> void:
##     var ctx := Netw.ctx(self)
##     var clock := ctx.services.get_clock()
##     var mgr := ctx.services.get_scene_manager()
## [/codeblock]
##
## [br][br]
## [b]Scene APIs[/b]
##
## Use [member NetwContext.scene] for lobby-level coordination:
## [codeblock]
## func _ready() -> void:
##     var ctx := Netw.ctx(self)
##     if not ctx.has_scene():
##         return
##     await ctx.scene.wait_for_players(4)
##     ctx.scene.start_countdown(5)
## [/codeblock]
##
## [br][br]
## [b]Logging and Tracing[/b]
##
## Use [member dbg] for structured logging and causal tracing.
## Pass format arguments as an [Array] to avoid expensive string
## formatting when the log level is suppressed:
## [codeblock]
## func _ready() -> void:
##     Netw.dbg.info(self, "Player ready: %s", [name])
## [/codeblock]
## [br]
##
## For repeated logging from the same object, create a handle:
## [codeblock]
## var _dbg: NetwHandle = Netw.dbg.handle(self)
##
## func do_work() -> void:
##     _dbg.info("Starting work")
##     var span := _dbg.span("setup")
##     # ... work ...
##     span.end()
## [/codeblock]
## [br]
##
## [b]Player Spawning[/b]
##
## Use [member spawn] for server-side player instantiation:
## [codeblock]
## func _on_player_joined(data: MultiplayerClientData) -> void:
##     var payload := Netw.spawn.gather(data, db, &"players")
##     var player := Netw.spawn.instantiate(payload, player_scene)
##     Netw.spawn.place(player, target_scene)
## [/codeblock]
## [br]
##
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
