## Serializable data bag describing a player attempting to connect to a session.
##
## Pass a populated instance to [method MultiplayerNetwork.connect_player] to
## authenticate and spawn a player, or serialize it for transmission via
## [method serialize].
class_name MultiplayerClientData
extends Serde

## The player's display name, used as the spawned node name prefix.
@export var username: StringName

## Path to the [SpawnerComponent] node the player should enter.
##
## [b]Note:[/b] The target [SpawnerComponent] must reside in a scene that
## tracks the owner scene correctly.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:SpawnerComponent")
var spawner_path: SceneNodePath

## Server URL to connect to. Leave empty or use [code]"localhost"[/code] for a
## local session.
@export var url: String

## Assigned by the server after receiving the connection request.
##
## [b]Note:[/b] This is not set by the client.
var peer_id: int

## When [code]true[/code], indicates this connection was initiated using debug 
## initialization data.
var is_debug: bool = false


## Serializes the client data into a [PackedByteArray] for network transmission.
func serialize() -> PackedByteArray:
	return var_to_bytes({
		username = username,
		spawner_path = spawner_path.as_uid(),
		url = url,
		peer_id = peer_id,
		is_debug = is_debug
	})


## Populates this object from a serialized [PackedByteArray] produced by
## [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)

	username = data.username
	spawner_path = SceneNodePath.new(data.spawner_path)
	url = data.url
	peer_id = data.peer_id
	is_debug = data.get("is_debug", false)


## Parses the multiplayer authority from a node name formatted as
## [code]username|peer_id[/code].
## Returns [param peer_id] as an [int], or [code]0[/code] if the name does
## not contain the separator.
static func parse_authority(node_name: String) -> int:
	var parts := node_name.split("|")
	if parts.size() == 2:
		return parts[1].to_int()
	return 0

