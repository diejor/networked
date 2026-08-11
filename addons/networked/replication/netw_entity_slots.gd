## The session's one entity numbering, shared by every core that needs a key.
##
## An entity's identity on a wire, in a column and in a native core is a small
## dense integer, and there must be exactly one of them. [member NetwEntity.rid]
## cannot be it: liveness leaves it unset until admission and clears it again at
## death, so several live entities hold an invalid one at once and keying on it
## merges them.
##
## A slot is minted once, never reused, and released only when every holder has
## let go. Holders are named because they retire on different cadences:
## [InterestCore] retires an entity when its owner leaves the tree, and a
## prediction engine is released only by an explicit
## [method NetwMultiplayer.predict_undeclare], so a book that retired on the
## first holder's cadence would hand the second a slot that resolves to nothing.
##
## [codeblock]
## var slot := slots.ensure(entity, &"interest")
## slots.release(entity, &"interest")   # marks retired if nobody else holds it
## slots.sweep()                        # drops the mapping, one cycle later
## [/codeblock]
##
## [b][br][br]A mapping outlives its removal by one full cycle.[/b] A removed
## entity's last act is a hide to every peer that held it, and those transitions
## arrive in the NEXT delta naming the slot. [method sweep] is what a caller runs
## once that delta has been applied, never at the removal itself.
class_name NetwEntitySlots
extends RefCounted

# Entity -> slot, and back. Both directions are needed: a delta names a slot and
# the policy that answers it needs the entity.
var _slots: Dictionary[NetwEntity, int] = { }
var _entities: Dictionary[int, NetwEntity] = { }

# Slot -> the holders that have not released it.
var _holders: Dictionary[int, Dictionary] = { }

# Slots every holder has released, awaiting the sweep.
var _retired: Dictionary[int, bool] = { }

# Monotonic, and never rewound by a release. A reused slot would let a late
# delta resolve to an entity that never earned it.
var _next: int = 1


## Mints [param entity]'s slot if it has none and records [param holder] as
## holding it.
func ensure(entity: NetwEntity, holder: StringName) -> int:
	if entity == null:
		return 0
	var slot: int = _slots.get(entity, 0)
	if slot == 0:
		slot = _next
		_next += 1
		_slots[entity] = slot
		_entities[slot] = entity
		_holders[slot] = { }
	_retired.erase(slot)
	(_holders[slot] as Dictionary)[holder] = true
	return slot


## The slot [param entity] already holds, or zero.
##
## Reads take this door so asking about an entity the session never numbered
## does not number it.
func slot_of(entity: NetwEntity) -> int:
	return int(_slots.get(entity, 0))


## The entity [param slot] names, or null once the slot has been swept.
func entity_for(slot: int) -> NetwEntity:
	return _entities.get(slot, null)


## Drops [param holder]'s claim on [param entity]'s slot, retiring the slot only
## once no holder is left.
func release(entity: NetwEntity, holder: StringName) -> void:
	var slot: int = _slots.get(entity, 0)
	if slot == 0:
		return
	var held: Dictionary = _holders.get(slot, { })
	held.erase(holder)
	if held.is_empty():
		_retired[slot] = true


## True while [param holder] still claims [param entity]'s slot.
func holds(entity: NetwEntity, holder: StringName) -> bool:
	var slot: int = _slots.get(entity, 0)
	return slot != 0 and (_holders.get(slot, { }) as Dictionary).has(holder)


## Drops every fully released mapping. Run this one full cycle after the
## release, never at the release itself.
func sweep() -> void:
	for slot: int in _retired:
		var entity := _entities.get(slot, null) as NetwEntity
		if entity:
			_slots.erase(entity)
		_entities.erase(slot)
		_holders.erase(slot)
	_retired.clear()


## Drops the whole numbering, which is what a new session is.
func clear() -> void:
	_slots.clear()
	_entities.clear()
	_holders.clear()
	_retired.clear()
	_next = 1
