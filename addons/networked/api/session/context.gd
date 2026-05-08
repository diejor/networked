## Composite context providing tree-level, service-level, scene-level,
## and entity-level access via [member tree], [member services],
## [member scene], and [member entity].
##
## Obtain via [method Netw.ctx] or [method for_node].
## [codeblock]
## var ctx := Netw.ctx(self)
## ctx.services.get_scene_manager()
## if ctx.has_scene():
##     await ctx.scene.wait_for_players(4)
## ctx.entity.collecting_spawn_properties.connect(_on_collect)
## [/codeblock]
##
## [b]Per-member nullability:[/b] Each facade resolves independently
## under its own constraints, so any of [member tree], [member services],
## [member scene], or [member entity] may be [code]null[/code]:
## [br]- [member tree], [member services] need an enclosing
##   [MultiplayerTree] in the live tree.
## [br]- [member scene] needs an enclosing [MultiplayerScene].
## [br]- [member entity] only needs a parent chain, so it resolves even
##   on orphans (e.g. during [constant Node.NOTIFICATION_PARENTED]).
##
## [b]Listen-Server checks:[/b]
## Access [member tree] to check [method NetwTree.is_listen_server] when
## writing custom RPCs.
class_name NetwContext
extends RefCounted

# ---------------------------------------------------------------------------
# Sub-contexts
# ---------------------------------------------------------------------------

## Session-level facade exposing multiplayer tree operations.
## [code]null[/code] when no enclosing [MultiplayerTree] is found.
var tree: NetwTree

## Service locator for backend systems registered on the
## [MultiplayerTree]. [code]null[/code] when no enclosing
## [MultiplayerTree] is found.
var services: NetwServices

## Scene-level facade for the current [MultiplayerScene], if any.
## [code]null[/code] when the node is not inside an active scene.
var scene: NetwScene

# Origin node passed to [method for_node]; used for lazy entity resolution.
var _origin: Node

## Per-owner orchestration hub. Resolves on first access by walking from
## [member _origin] to the entity root. [code]null[/code] when
## [member _origin] is invalid.
var entity: NetwEntity:
	get: return NetwEntity.of(_origin) if is_instance_valid(_origin) else null


func _init(
		mt: MultiplayerTree = null,
		scene_ctx: NetwScene = null,
		origin: Node = null,
) -> void:
	if mt:
		tree = NetwTree.new(mt)
		services = NetwServices.new(mt)
	scene = scene_ctx
	_origin = origin


# ---------------------------------------------------------------------------
# Validity
# ---------------------------------------------------------------------------

## Returns [code]true[/code] if every facade present on this context is
## still valid. A context whose facades are individually [code]null[/code]
## (e.g. orphan node, no scene) is still considered valid -- callers
## null-check the specific member they need.
func is_valid() -> bool:
	if tree and not tree.is_valid():
		return false
	if services and not services.is_valid():
		return false
	if scene and not scene.is_valid():
		return false
	return true


## Returns [code]true[/code] while [member scene] is set and alive.
func has_scene() -> bool:
	return scene != null and scene.is_valid()


# ---------------------------------------------------------------------------
# Static access
# ---------------------------------------------------------------------------

## Returns a [NetwContext] for [param node].
##
## Always returns a non-null context; individual facade members may be
## [code]null[/code] when their underlying source is unreachable from
## [param node]. See class docs for per-member rules.
static func for_node(node: Node) -> NetwContext:
	var mt := MultiplayerTree.for_node(node)
	var scene_node := MultiplayerTree.scene_for_node(node)
	var scene_ctx: NetwScene = null
	if is_instance_valid(scene_node):
		var sc := scene_node.get_context()
		if sc:
			scene_ctx = sc.scene
	return NetwContext.new(mt, scene_ctx, node)
