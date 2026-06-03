## Lookup facade for one [MultiplayerTree]'s interest graph.
##
## Exposed at [member MultiplayerTree.interest]. This object only
## resolves layers; [NetwInterestLayer] owns mutation, policy, and
## transition signals.
##
## [br][br]
## Server code usually creates layers through [method layer]. Client
## code should only rely on layers mirrored by an [InterestGate], or on
## observer signals relayed by [member InterestComponent.report_observers].
## [codeblock]
## var arena := Netw.ctx(self).interest.layer(&"arena")
## arena.add_entity(player_entity)
## arena.add_viewer(player.peer_id)
## [/codeblock]
class_name NetwInterest
extends RefCounted

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns the layer for [param layer_id], creating it on first use.
func layer(layer_id: StringName) -> NetwInterestLayer:
	var service := _service()
	return service.layer_for(layer_id) if service else null


## Returns the layer for [param layer_id], or [code]null[/code].
func get_layer(layer_id: StringName) -> NetwInterestLayer:
	var service := _service()
	return service.get_layer(layer_id) if service else null


## Returns every known layer.
func all_layers() -> Array[NetwInterestLayer]:
	var service := _service()
	if service:
		return service.all_layers()
	var out: Array[NetwInterestLayer] = []
	return out


func _service() -> InterestService:
	var mt := _tree()
	if not mt:
		return null
	var service := mt.get_service(InterestService) as InterestService
	if service:
		return service
	return mt.find_service_node(InterestService) as InterestService


func _tree() -> MultiplayerTree:
	return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null
