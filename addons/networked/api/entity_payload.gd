## Serializable payload for [method NetwSpawn.spawn_entity] and for
## entities passing through a custom [code]spawn_function[/code].
##
## Parallel to [SpawnPayload] but without [code]username[/code] /
## [code]peer_id[/code]: entities are not peer-bound. Travels as a
## [Dictionary] when forwarded through [method MultiplayerSpawner.spawn].
class_name EntityPayload
extends RefCounted

## Stable identifier for this entity class. Used by user code to route
## scene loading and save-table lookups.
var class_id: StringName

## State to apply via [SaveComponent.hydrate]. Empty for a fresh entity.
var save_state: Dictionary

## User data merged at the call site. Values must be Godot-serializable
## when forwarded through a [MultiplayerSpawner].
var extras: Dictionary


func _init(
	p_class_id: StringName = &"",
	p_save_state: Dictionary = {},
	p_extras: Dictionary = {},
) -> void:
	class_id = p_class_id
	save_state = p_save_state
	extras = p_extras


## Serialises this payload into a [Dictionary] suitable for
## [method MultiplayerSpawner.spawn].
func to_variant() -> Dictionary:
	var dict: Dictionary = {
		&"class_id": String(class_id),
	}
	if not save_state.is_empty():
		dict[&"save_state"] = save_state
	if not extras.is_empty():
		dict[&"extras"] = extras
	return dict


## Reconstructs an [EntityPayload] from a [Dictionary] produced by
## [method to_variant].
static func from_variant(v: Variant) -> EntityPayload:
	var d: Dictionary = v
	return EntityPayload.new(
		StringName(d.get("class_id", "")),
		d.get("save_state", {}),
		d.get("extras", {}),
	)
