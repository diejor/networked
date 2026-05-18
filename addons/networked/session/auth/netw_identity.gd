## Validated player identity produced by an auth provider.
##
## Returned by [method NetwAuthProvider._authenticate] and stored in
## [NetwIdentityBucket] on the server. The [member username] field is
## server-authoritative -- [method MultiplayerTree.request_join_player]
## uses it to override the client-claimed username.
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


## Populates a new [NetwIdentity] from a serialized [PackedByteArray].
static func deserialize(bytes: PackedByteArray) -> NetwIdentity:
	var data := bytes_to_var(bytes)
	assert(data)
	var identity := NetwIdentity.new()
	identity.username = data.username
	identity.external_id = data.external_id
	identity.service = data.service
	identity.metadata = data.get("metadata", {})
	return identity
