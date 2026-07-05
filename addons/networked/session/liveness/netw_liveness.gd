## Liveness facade for one [MultiplayerTree]: who exists, where, and under
## which wire route.
##
## Exposed at [member MultiplayerTree.liveness] and [member NetwContext.liveness].
## This object only answers questions and defers callbacks. [LivenessService]
## owns the registry, the state machine, and the rules for how routes are
## allocated and travel, so read its class description for the model.
## [codeblock]
## var liveness := Netw.ctx(self).liveness
##
## # Name an entity compactly on the wire.
## var route := liveness.route_of(entity)
##
## # Resolve a received route, tolerating the spawn window.
## var target := liveness.entity_of(route)
## if target == null:
##     liveness.when_live(route, func():
##         apply(liveness.entity_of(route))
##     )
## [/codeblock]
class_name NetwLiveness
extends RefCounted

## Emitted when an entity route transitions to [constant LivenessService.State.LIVE].
signal entity_live(route: int, entity: NetwEntity)

## Emitted when an entity route transitions to [constant LivenessService.State.LINGERING].
signal entity_lingering(route: int, entity: NetwEntity)

## Emitted when an entity route transitions to [constant LivenessService.State.DEAD].
signal entity_dead(route: int)

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns the route ID bound to [param entity], or [code]0[/code].
func route_of(entity: NetwEntity) -> int:
	var service := _service()
	return service.route_of(entity) if service else 0


## Returns the [NetwEntity] bound to [param route], or [code]null[/code].
func entity_of(route: int) -> NetwEntity:
	var service := _service()
	return service.entity_of(route) if service else null


## Returns the [Node] associated with [param route], or [code]null[/code].
func node_of(route: int) -> Node:
	var service := _service()
	return service.node_of(route) if service else null


## Returns the local existence state of [param entity]. See
## [enum LivenessService.State].
func state_of(entity: NetwEntity) -> LivenessService.State:
	var service := _service()
	return service.state_of(entity) if service else LivenessService.State.UNKNOWN


## Returns the state of [param route].
func route_state(route: int) -> LivenessService.State:
	var service := _service()
	return service.route_state(route) if service else LivenessService.State.UNKNOWN


## Returns the send gate for [param entity] toward [param peer_id]. See
## [method LivenessService.is_live_for] for the optimism contract.
func is_live_for(peer_id: int, entity: NetwEntity) -> bool:
	var service := _service()
	return service.is_live_for(peer_id, entity) if service else false


## Returns peers for whom [param entity] is live.
func live_peers(entity: NetwEntity) -> Array[int]:
	var service := _service()
	return service.live_peers(entity) if service else []


## Runs [param cb] once [param route] is live locally. See
## [method LivenessService.when_live] for the timeout rule.
func when_live(route: int, cb: Callable, timeout_ticks: int = 0) -> void:
	var service := _service()
	if service:
		service.when_live(route, cb, timeout_ticks)


func _service() -> LivenessService:
	var mt := _tree()
	if not mt:
		return null
	var service := mt.get_service(LivenessService) as LivenessService
	if service:
		return service
	return mt.find_service_node(LivenessService) as LivenessService


func _tree() -> MultiplayerTree:
	return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null
