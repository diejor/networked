## Serializable in-game join data describing a player entering a session.
##
## Pass a populated instance to [method NetwConnector.join],
## [method MultiplayerTree.host_player], or
## [method NetwConnector.join_or_host]. Transport identity is supplied
## separately by a [NetwConnectTarget] and is not part of this payload.
class_name JoinPayload
extends Serde

## The player's display name, used as the spawned node name prefix.
@export var username: StringName

## Typed join args the session's registered handler receives after the
## [ResolvedJoin]. The client fills these from its handler's wire schema. Empty
## when the client expresses no join intent. Encoded to [member arg_bytes] by
## the session before the request travels, so this field itself never rides the
## wire.
var arg_values: Array = []

## The wire-encoded form of [member arg_values], packed by the session against
## its resolved handler's schema. Empty when there is no join intent.
@export var arg_bytes: PackedByteArray = PackedByteArray()

## Identity of the schema [member arg_bytes] was packed against, used to reject a
## client and server that disagree on the join handler signature.
@export var schema_hash: int = 0

## Assigned by the server after receiving the connection request.
##
## [b]Note:[/b] This is not set by the client.
var peer_id: int

## When [code]true[/code], indicates this connection was initiated using debug
## initialization data.
var is_debug: bool = false


## Validates structural fields and produces a [ResolvedJoin].
##
## Returns [code]null[/code] if [member username] is empty. [member arg_values]
## is copied through; an empty array means no join intent.
func resolve() -> ResolvedJoin:
	if username.is_empty():
		return null
	var rj := ResolvedJoin.new()
	rj.peer_id = peer_id
	rj.username = username
	rj.is_debug = is_debug
	rj.arg_values = arg_values.duplicate(true)
	return rj


## Serializes the join payload into a [PackedByteArray] for network
## transmission. Carries the encoded [member arg_bytes], not the live
## [member arg_values].
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		username = username,
		arg_bytes = arg_bytes,
		schema_hash = schema_hash,
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
	arg_bytes = data.get("arg_bytes", PackedByteArray())
	schema_hash = int(data.get("schema_hash", 0))
	peer_id = data.peer_id
	is_debug = data.get("is_debug", false)
