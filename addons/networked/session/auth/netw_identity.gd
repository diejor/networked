## Validated player identity produced by an auth flow.
##
## Carried by an [AuthResult] from [method NetwAuthFlow.verify] and stored in
## [NetwIdentityBucket]. [AuthCoordinator] treats [member username] as
## server-authoritative when [MultiplayerTree] accepts a player.
class_name NetwIdentity
extends RefCounted

## Display name for spawn and UI.
var username: StringName

## Provider-specific external player ID (e.g. Steam ID, Discord user ID).
var external_id: String

## Auth service that produced this identity.
var service: StringName

## Opaque provider metadata, forwarded from the provider.
var metadata: Dictionary


## Serializes this identity into a [PackedByteArray].
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		username = username,
		external_id = external_id,
		service = service,
		metadata = metadata,
	}
	return var_to_bytes(dict)


## Returns a stable display name for [param node].
static func username_of(node: Node) -> String:
	if not is_instance_valid(node):
		return ""
	var entity := NetwEntity.of(node)
	if entity and not entity.entity_id.is_empty():
		return entity.entity_id
	return node.name.get_slice("|", 0)


## Returns a stable debugger key for [param node].
static func stable_id_of(node: Node) -> Variant:
	if not is_instance_valid(node):
		return ""
	var entity := NetwEntity.of(node)
	if entity and entity.peer_id != 0:
		return entity.peer_id
	var parsed := NetwEntity.parse_peer(node.name)
	return parsed if parsed != 0 else str(node.get_path())


## Populates a new [NetwIdentity] from a serialized [PackedByteArray].
static func deserialize(bytes: PackedByteArray) -> NetwIdentity:
	var data := bytes_to_var(bytes)
	assert(data)
	var identity := NetwIdentity.new()
	identity.username = data.username
	identity.external_id = data.external_id
	identity.service = data.service
	identity.metadata = data.get("metadata", { })
	return identity
