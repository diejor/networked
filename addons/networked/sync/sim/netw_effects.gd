## Keyed optimistic effect ledger for predicted actions.
##
## [LagCompensationService] owns the pending state so one [MultiplayerTree]
## resolves every [method arm], [method adopt], and [method discard] through the
## same tick clock. [NetwAction] uses this ledger for command prediction, while
## custom transports can use the keyed primitive directly.
##
## [codeblock]
## var lag := Netw.ctx(self).lag_compensation
## var key := lag.effects.key_for(Netw.ctx(self).entity, view_tick)
## lag.effects.arm(key, func() -> void: ghost.queue_free())
## # Later, authoritative transport resolves the same key.
## lag.effects.adopt(key)
## [/codeblock]
##
## When no [LagCompensationService] is mounted, the methods degrade to safe
## no-ops and [method key_for] still returns a deterministic [StringName].
class_name NetwEffects
extends RefCounted

var _service_ref: WeakRef


func _init(service: LagCompensationService = null) -> void:
	_service_ref = weakref(service) if service else null


## Returns a deterministic action key for [param entity], [param tick], and
## [param slot].
func key_for(entity: NetwEntity, tick: int, slot: int = 0) -> StringName:
	if not entity:
		return StringName("act__%d__%d" % [tick, slot])
	return StringName("act__%s__%d__%d" % [entity.entity_id, tick, slot])


## Registers [param key] with [param revert] until it is resolved or times out.
func arm(
		key: StringName,
		revert: Callable,
		timeout_ticks: int = 0,
) -> void:
	var service := _service()
	if service:
		service._effect_arm(key, revert, timeout_ticks)


## Resolves [param key] as kept. The pending [param key] is removed.
func adopt(key: StringName) -> void:
	var service := _service()
	if service:
		service._effect_adopt(key)


## Resolves [param key] as reverted. The pending revert runs immediately.
func discard(key: StringName) -> void:
	var service := _service()
	if service:
		service._effect_discard(key)


func _service() -> LagCompensationService:
	return _service_ref.get_ref() as LagCompensationService if _service_ref else null
