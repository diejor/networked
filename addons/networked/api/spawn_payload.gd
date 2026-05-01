## Serializable payload produced by [method NetwSpawn.gather] and consumed
## by [method NetwSpawn.configure].
##
## Travels through [method MultiplayerSpawner.spawn] as a [Dictionary]
## when using custom spawn functions.
class_name SpawnPayload
extends RefCounted

## The player's display name.
var username: StringName

## The peer ID assigned by the server.
var peer_id: int

## Entity record from the database, or an empty [Dictionary] for a fresh
## player.
var save_state: Dictionary

## Optional user data merged during [method NetwSpawn.gather].
## Values must be Godot-serializable when forwarded through a spawner.
var extras: Dictionary


func _init(
	p_username: StringName,
	p_peer_id: int,
	p_save_state: Dictionary = {},
	p_extras: Dictionary = {},
) -> void:
	username = p_username
	peer_id = p_peer_id
	save_state = p_save_state
	extras = p_extras


## Serialises this payload into a [Dictionary] suitable for
## [method MultiplayerSpawner.spawn].
func to_variant() -> Dictionary:
	var dict: Dictionary = {
		&"username": String(username),
		&"peer_id": peer_id,
	}
	if not save_state.is_empty():
		dict[&"save_state"] = save_state
	if not extras.is_empty():
		dict[&"extras"] = extras
	return dict


## Reconstructs a [SpawnPayload] from a [Dictionary] produced by
## [method to_variant].
static func from_variant(v: Variant) -> SpawnPayload:
	var d: Dictionary = v
	return SpawnPayload.new(
		d.get("username", ""),
		d.get("peer_id", 0),
		d.get("save_state", {}),
		d.get("extras", {}),
	)
