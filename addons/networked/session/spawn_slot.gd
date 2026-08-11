## Carries both the causal token and the placement target for a player spawn.
##
## Obtained via [method MultiplayerTree.get_spawn_slot].
class_name SpawnSlot
extends RefCounted

## Causal [CheckpointToken] for span tracing. May be [code]null[/code].
var token: CheckpointToken

var _scene: NetwSceneHandle
var _parent_node: Node


func is_valid() -> bool:
	return has_scene() or is_instance_valid(_parent_node)


func has_scene() -> bool:
	return _scene != null and _scene.is_declared


## Returns the resolved [NetwSceneHandle], or [code]null[/code].
func get_scene() -> NetwSceneHandle:
	return _scene if has_scene() else null


## Adds [param player] to the scene via
## [method NetwSceneHandle.add_player], or directly to [member _parent_node] if
## no scene is set.
##
## Closes [param span] with [method NetwSpan.end] when provided.
func place_player(player: Node, span: NetwSpan = null) -> void:
	if has_scene():
		_scene.add_player(NetwEntity.of(player))
	elif is_instance_valid(_parent_node):
		_parent_node.add_child(player)
	if span:
		span.end()
