## Service locator facade over [MultiplayerTree].
##
## Provides access to backend services such as [MultiplayerSceneManager]
## and [NetworkClock] without exposing the concrete [MultiplayerTree] class
## to component code.
## [br][br]
## Obtain via [member NetwContext.services] rather than constructing directly.
## Holds a [WeakRef] so cached references do not keep the tree alive.
class_name NetwServices
extends RefCounted

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return is_instance_valid(_tree_ref.get_ref())


## Returns the [MultiplayerSceneManager] service, or [code]null[/code].
func get_scene_manager() -> MultiplayerSceneManager:
	var mt: MultiplayerTree = _tree_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(MultiplayerSceneManager)


## Returns the [NetworkClock] service, or [code]null[/code].
func get_clock() -> NetworkClock:
	var mt: MultiplayerTree = _tree_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(NetworkClock)


## Returns the [NetwPeerContext] for [param peer_id].
func get_peer_context(peer_id: int) -> NetwPeerContext:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_peer_context(peer_id) if mt else null


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_service(type) if mt else null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(
	spawner_path: SceneNodePath
) -> MultiplayerTree.SpawnSlot:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return MultiplayerTree.SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)
