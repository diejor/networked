## Serializable in-game join data describing a player entering a session.
##
## Pass a populated instance to [method MultiplayerTree.join],
## [method MultiplayerTree.host_player], or
## [method MultiplayerTree.join_or_host]. Transport identity is supplied
## separately by a [JoinTarget] and is not part of this payload.
class_name JoinPayload
extends Serde

## The player's display name, used as the spawned node name prefix.
@export var username: StringName

## Opaque spawn intent, produced by a [SpawnPolicy]'s
## [method SpawnPolicy.to_dict]. The server's configured
## [member MultiplayerTree.spawn_policy] interprets it. Empty when
## the client expresses no spawn intent.
@export var spawn: Dictionary = { }

## Assigned by the server after receiving the connection request.
##
## [b]Note:[/b] This is not set by the client.
var peer_id: int

## When [code]true[/code], indicates this connection was initiated using debug
## initialization data.
var is_debug: bool = false


## Validates structural fields and produces a [ResolvedJoin].
##
## Returns [code]null[/code] if [member username] is empty. [member spawn] is
## copied through verbatim; an empty dictionary means no spawn intent.
func resolve() -> ResolvedJoin:
	if username.is_empty():
		return null
	var rj := ResolvedJoin.new()
	rj.peer_id = peer_id
	rj.username = username
	rj.is_debug = is_debug
	rj.spawn = spawn.duplicate(true)
	return rj


## Serializes the join payload into a [PackedByteArray] for network
## transmission.
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		username = username,
		spawn = spawn,
		peer_id = peer_id,
		is_debug = is_debug,
	}
	return var_to_bytes(dict)


## Populates this object from a serialized [PackedByteArray] produced by
## [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)

	username = data.username
	spawn = data.get("spawn", { })
	peer_id = data.peer_id
	is_debug = data.get("is_debug", false)
