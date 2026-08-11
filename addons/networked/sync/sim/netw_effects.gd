## Object face for the session's ledger of optimistic acts.
##
## The ledger itself is the session's, reached through
## [method NetwMultiplayer.effect_arm] and its siblings. This carries no state
## of its own, so a component that would rather hold one object than a session
## reference and four verb names can pass it around and still resolve the same
## keys everything else resolves.
##
## [codeblock]
## var effects := NetwEffects.new(Netw.of(self))
## var key := effects.key_for(NetwEntity.of(self), view_tick)
## effects.arm(key, func() -> void: ghost.queue_free())
## # Later, the authoritative reply resolves the same key.
## effects.adopt(key)
## [/codeblock]
##
## Built with no session, every method degrades to a safe no-op and
## [method key_for] still returns the same deterministic [StringName].
class_name NetwEffects
extends RefCounted

var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


## Returns the ledger key for an act by [param entity] at [param tick].
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
	var api := _api()
	if api:
		api.effect_arm(key, revert, timeout_ticks)


## Resolves [param key] as kept. The pending revert is dropped unrun.
func adopt(key: StringName) -> void:
	var api := _api()
	if api:
		api.effect_adopt(key)


## Resolves [param key] as reverted. The pending revert runs immediately.
func discard(key: StringName) -> void:
	var api := _api()
	if api:
		api.effect_discard(key)


## Returns whether [param key] is armed and unresolved.
func pending(key: StringName) -> bool:
	var api := _api()
	return api.effect_pending(key) if api else false


func _api() -> NetwMultiplayer:
	return _api_ref.get_ref() as NetwMultiplayer if _api_ref else null
