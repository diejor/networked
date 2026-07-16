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

## Typed join args the session's registered handler receives after this
## [ResolvedJoin]. Empty when no join intent was provided. See
## [member JoinPayload.arg_values].
var arg_values: Array = []

## Whether this connection used debug initialization data.
var is_debug: bool


## Serializes this resolved join into a [PackedByteArray] for network
## transmission or storage.
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		peer_id = peer_id,
		username = username,
		arg_values = arg_values,
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
	rj.arg_values = data.get("arg_values", [])
	rj.is_debug = data.get("is_debug", false)
	return rj
