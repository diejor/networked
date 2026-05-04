## Composite context providing tree-level, service-level, and scene-level
## access via [member tree], [member services], and [member scene].
##
## Obtain via [method Netw.ctx] or [method for_node].
## [codeblock]
## var ctx := Netw.ctx(self)
## ctx.services.get_scene_manager()
## if ctx.has_scene():
##     await ctx.scene.wait_for_players(4)
## [/codeblock]
class_name NetwContext
extends RefCounted

# ---------------------------------------------------------------------------
# Sub-contexts
# ---------------------------------------------------------------------------

## Session-level facade exposing multiplayer tree operations.
##
## [br][br]
## Provides gameplay APIs such as [method NetwTree.pause],
## [method NetwTree.kick], and session introspection
## ([method NetwTree.is_server], [method NetwTree.get_state]).
var tree: NetwTree

## Service locator for backend systems registered on the
## [MultiplayerTree].
##
## [br][br]
## Holds a [WeakRef] so cached contexts do not keep the tree alive.
var services: NetwServices

## Scene-level facade for the current [MultiplayerScene], if any.
##
## [br][br]
## Provides lobby APIs such as [method NetwScene.wait_for_players],
## [method NetwScene.suspend], and [method NetwScene.start_countdown].
##
## [br][br]
## [b]Note:[/b] This is [code]null[/code] when the node is not inside an
## active scene. Check [method has_scene] before accessing.
var scene: NetwScene


func _init(mt: MultiplayerTree, scene_ctx: NetwScene = null) -> void:
	tree = NetwTree.new(mt)
	services = NetwServices.new(mt)
	if scene_ctx:
		scene = scene_ctx


# ---------------------------------------------------------------------------
# Validity
# ---------------------------------------------------------------------------

## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive and, if a scene is present, while the underlying [MultiplayerScene]
## is also alive.
func is_valid() -> bool:
	return (
		tree != null and tree.is_valid()
		and services != null and services.is_valid()
		and (not has_scene() or scene.is_valid())
	)


## Returns [code]true[/code] while the underlying [Scene] is still alive.
func has_scene() -> bool:
	return scene != null and scene.is_valid()


# ---------------------------------------------------------------------------
# Static access
# ---------------------------------------------------------------------------

## Returns a [NetwContext] for [param node] by walking its ancestor chain.
##
## Returns [code]null[/code] if [param node] is not inside a multiplayer
## session.
static func for_node(node: Node) -> NetwContext:
	var scene := MultiplayerTree.scene_for_node(node)
	return scene.get_context() if is_instance_valid(scene) else null
