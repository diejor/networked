## Composite context providing both tree-level and scene-level access via
## [member tree] and [member scene].
##
## Obtain via [method Netw.ctx] or [method for_node].
## [codeblock]
## var ctx := Netw.ctx(self)
## ctx.tree.get_service(MyService)
## if ctx.has_scene():
##     await ctx.scene.wait_for_players(4)
## [/codeblock]
class_name NetwContext
extends RefCounted

# ---------------------------------------------------------------------------
# Sub-contexts
# ---------------------------------------------------------------------------

var tree: NetwTree
var scene: NetwScene


func _init(mt: MultiplayerTree, scene_ctx: NetwScene = null) -> void:
	tree = NetwTree.new(mt)
	if scene_ctx:
		scene = scene_ctx


# ---------------------------------------------------------------------------
# Validity
# ---------------------------------------------------------------------------

## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return tree != null and tree.is_valid()


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
