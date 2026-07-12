## A wire reference to a node inside a [NetwEntity], the value a [Node] or
## [NetwEntity] becomes when it crosses as an argument, spawn argument, or
## request return value.
##
## A [Node] cannot be serialized, and a peer's copy of an entity is a different
## instance anyway, so a node argument travels as its entity route plus the
## component addressing that locates the node under
## [member NetwEntity.owner]. The receiver rebinds it to its own instance
## through [method NetwReplicationInterface.resolve_comp_node]. This is a
## distinct codec kind rather than a sentinel dictionary, so it never collides
## with a user value.
## [codeblock]
## route  the entity's session route
## comp   0 root, 1..254 a registered component id, 255 path fallback
## path   relative path under the root, only when comp is 255
## [/codeblock]
class_name NetwNodeRef
extends RefCounted

## The entity route the referenced node belongs to.
var route: int

## The component address under the entity root: [code]0[/code] is the root,
## [code]1[/code] to [code]254[/code] index the registered component table, and
## [code]255[/code] means read [member path].
var comp: int

## The relative path under the entity root, used only when [member comp] is
## [code]255[/code].
var path: String


func _init(p_route: int = 0, p_comp: int = 0, p_path: String = "") -> void:
	route = p_route
	comp = p_comp
	path = p_path
