## Validated join data with all fields guaranteed non-null.
##
## Produced by [method JoinPayload.resolve]. Downstream code asserts on
## these values rather than null-checking.
## [br][br]
## Serialized for network transport and stored in [MultiplayerTree]'s
## joined-player registry.
class_name ResolvedJoin
extends RefCounted

## Assigned server peer ID for this player.
var peer_id: int

## The player's display name.
var username: StringName

## Name of the target scene (derived from spawner_component_path).
## Empty when no spawner path was provided.
var scene_name: StringName

## Node path to the spawner within the target scene.
## Empty when no spawner path was provided.
var spawner_path: NodePath

## Whether this connection used debug initialization data.
var is_debug: bool


## Serializes this resolved join into a [PackedByteArray] for network
## transmission or storage.
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		peer_id = peer_id,
		username = username,
		scene_name = scene_name,
		spawner_path = spawner_path,
		is_debug = is_debug,
	}
	return var_to_bytes(dict)


## Populates a new [ResolvedJoin] from a serialized [PackedByteArray].
static func deserialize(bytes: PackedByteArray) -> ResolvedJoin:
	var data := bytes_to_var(bytes)
	assert(data)
	var rj := ResolvedJoin.new()
	rj.peer_id = data.peer_id
	rj.username = data.username
	rj.scene_name = data.scene_name
	rj.spawner_path = data.spawner_path
	rj.is_debug = data.get("is_debug", false)
	return rj
