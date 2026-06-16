## Read-only history queries over the [TimelineRegistry], the server-side lag
## compensation surface.
##
## Lag compensation is a query, not a system: with the server recording
## authoritative snapshots every tick, answering "where was this entity when the
## shooter saw it" is a timeline read. Analytic [method sample] is the default and
## reads history without touching the scene. Scoped scene rewind is the opt-in
## heavyweight that briefly applies past state to live nodes.
##
## [codeblock]
## var past := queries.sample(entity, view_tick)   # detached NetwSnapshot
## queries.rewind(targets, view_tick, func():       # scoped, restored on return
##     ... )
## [/codeblock]
##
## Owned by [LagCompensationService]. A consumer of the registry, never a producer.
##
## [br][br][b]Server Only.[/b]
class_name RewindQueries
extends RefCounted

var _registry: TimelineRegistry


func _init(registry: TimelineRegistry) -> void:
	_registry = registry


## Returns [param entity]'s recorded state at or before [param tick] as a detached
## [NetwSnapshot], reading history without touching the live scene.
##
## The snapshot carries forward, so a tick between recordings reads the latest
## prior state. A view tick older than the retained window, an unregistered entity,
## or a call off the server (where no history is recorded) all return an empty
## snapshot, the neutral signal a caller clamps against rather than a fabricated
## position.
##
## [codeblock]
## var past := queries.sample(target, shooter_view_tick)
## if past.has_value(&"position") and hits(origin, dir, past.position):
##     apply_damage(target)
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func sample(entity: NetwEntity, tick: int) -> NetwSnapshot:
	var tl := _registry.of(entity)
	if not tl:
		return NetwSnapshot.new()
	return NetwSnapshot.from_dictionary(tl.latest_state_at_or_before(tick))


## Briefly applies each entity's state at [param tick] to its live node, runs
## [param body], then restores the live state, unconditionally, on return.
##
## This is the opt-in heavyweight query for validation that needs real physics, for
## example a [method PhysicsDirectSpaceState2D.intersect_ray] against complex
## collision shapes. The targets hold their rewound transform only inside
## [param body]. Analytic [method sample] is the default and should be preferred
## when a position read suffices, because scene rewind mutates live nodes and
## depends on [method Node3D.force_update_transform] to make the rewound transform
## visible to a space-state query. An entity with no retained history at
## [param tick] is left at its live state and skipped.
##
## [codeblock]
## queries.rewind(targets, shooter_view_tick, func() -> void:
##     var hit := space.intersect_ray(query)   # targets are at the perceived tick
##     ... )
## [/codeblock]
##
## [br][br][b]Server Only.[/b]
func rewind(entities: Array[NetwEntity], tick: int, body: Callable) -> void:
	# Capture only the entities we actually rewind, so restore touches exactly the
	# nodes we moved and an entity with no history stays at its live state.
	var captured: Array[Dictionary] = []
	for entity in entities:
		var state := entity.state
		if not state:
			continue
		var tl := _registry.of(entity)
		if not tl:
			continue
		var snap := tl.latest_state_at_or_before(tick)
		if snap.is_empty():
			continue
		captured.append({ &"entity": entity, &"live": state.snapshot_payload() })
		state.apply_payload(snap)
		_force_update_transform(entity)

	body.call()

	for entry in captured:
		var entity: NetwEntity = entry[&"entity"]
		var state := entity.state
		if not state:
			continue
		state.apply_payload(entry[&"live"])
		_force_update_transform(entity)


# Pushes a rewound or restored transform into the physics server so a space-state
# query inside the callable sees it. Godot's direct space state reflects the last
# physics sync, so this must run after each apply.
func _force_update_transform(entity: NetwEntity) -> void:
	var node := entity.owner
	if is_instance_valid(node) and node.has_method(&"force_update_transform"):
		node.force_update_transform()
