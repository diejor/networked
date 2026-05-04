## Carries both the causal token and the placement target for a player spawn.
##
## Obtained via [method MultiplayerTree.get_spawn_slot].
class_name SpawnSlot
extends RefCounted

## Causal [CheckpointToken] for span tracing. May be [code]null[/code].
var token: CheckpointToken

var _scene: MultiplayerScene
var _parent_node: Node


func is_valid() -> bool:
	return is_instance_valid(_scene) or is_instance_valid(_parent_node)


func has_scene() -> bool:
	return is_instance_valid(_scene)


## Returns the resolved [MultiplayerScene], or [code]null[/code].
func get_scene() -> MultiplayerScene:
	return _scene if is_instance_valid(_scene) else null


## Adds [param player] to the scene via [method Scene.add_player],
## or directly to [member _parent_node] if no scene is set.
##
## Closes [param span] with [method NetSpan.end] when provided.
func place_player(player: Node, span: NetSpan = null) -> void:
	if is_instance_valid(_scene):
		_scene.add_player(player)
	elif is_instance_valid(_parent_node):
		_parent_node.add_child(player)
	if span:
		span.end()
