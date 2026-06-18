## Public lag-compensation facade for one [MultiplayerTree].
##
## Exposed at [member NetwContext.lag_compensation]. Lag compensation is a query
## and action surface. With the server recording authoritative history every
## tick, this facade answers "where was an entity when the shooter saw it" and
## lets [NetwAction] correlate optimistic effects with authoritative results.
##
## [codeblock]
## var lag := Netw.ctx(self).lag_compensation
## var past := lag.sample(target_entity, view_tick)   # detached NetwSnapshot
## if past.has_value(&"position"):
##     validate_hit(origin, dir, past.position)
##
## var action := lag.action(_place_bomb)
## action.predict = _predict_bomb
## action.request(view_tick)
## [/codeblock]
##
## Resolves the backing [LagCompensationService] lazily, like [NetwInterest], so it
## degrades to safe empty results when no service is mounted.
class_name NetwLagCompensation
extends RefCounted

var _tree_ref: WeakRef

## Keyed optimistic effects owned by [LagCompensationService].
var effects: NetwEffects:
	get:
		var service := _service()
		return service.effects if service else NetwEffects.new()


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot], the analytic hit-validation read. See [method RewindQueries.sample].
##
## Returns an empty [NetwSnapshot] when no service is mounted, off the server, or
## for an entity with no retained history at [param tick].
##
## [codeblock]
## var past := Netw.ctx(self).lag_compensation.sample(target, view_tick)
## if past.has_value(&"position") and hits(origin, dir, past.position):
##     apply_damage(target)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	var service := _service()
	return service.sample(entity, tick) if service else NetwSnapshot.new()


## Applies each entity's state at [param tick] to its live node for the duration of
## [param body], then restores it. The opt-in heavyweight alternative to
## [method sample] for validation that needs real physics queries. See
## [method RewindQueries.rewind]. A no-op when no service is mounted.
##
## [codeblock]
## Netw.ctx(self).lag_compensation.rewind(targets, view_tick, func() -> void:
##     var hit := space.intersect_ray(query)   # targets at the perceived tick
##     ... )
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
	var service := _service()
	if service:
		service.rewind(entities, tick, body)


## Returns the registered [NetwTimeline] for [param entity], or [code]null[/code].
func timeline_of(entity: NetwEntity) -> NetwTimeline:
	var service := _service()
	return service.timeline_of(entity) if service else null


## Returns a [NetwAction] bound to [param authority].
##
## [param authority] must be a method [Callable] on the entity root or one of
## its children. The returned action uses [member effects] for local prediction
## and the mounted [LagCompensationService] for private request transport.
func action(authority: Callable) -> NetwAction:
	var service := _service()
	var slot := service._assign_action_slot(authority) if service else 0
	return NetwAction.new(self, authority, slot)


func _service() -> LagCompensationService:
	var mt := _tree()
	if not mt:
		return null
	var service := mt.get_service(LagCompensationService) as LagCompensationService
	if service:
		return service
	return mt.find_service_node(LagCompensationService) as LagCompensationService


func _tree() -> MultiplayerTree:
	return _tree_ref.get_ref() as MultiplayerTree if _tree_ref else null
