## Serializable data bag describing a player attempting to connect to a session.
##
## Pass a populated instance to [method MultiplayerNetwork.connect_player] to authenticate
## and spawn a player, or serialize it for transmission via [method serialize].
class_name MultiplayerClientData
extends Serde

## The player's display name, used as the spawned node name prefix.
@export var username: StringName

## Path to the [ClientComponent] spawner node the player should enter.
##
## TODO: somehow assert or show warnings if the target ClientComponnet lives in
## a scene that doesnt track owner scene (somehow check this at the moment of
## spawning).
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:ClientComponent")
var spawner_path: SceneNodePath

## Server URL to connect to. Leave empty or use [code]"localhost"[/code] for a local session.
@export var url: String

## Assigned by the server after receiving the connection request. Not set by the client.
var peer_id: int

## Serializes the client data into a [PackedByteArray] for network transmission.
func serialize() -> PackedByteArray:
	return var_to_bytes({
		username=username, 
		spawner_path=spawner_path.as_uid(),
		url=url,
		peer_id=peer_id
	})

## Populates this object from a serialized [PackedByteArray] produced by [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)
	
	username = data.username
	spawner_path = SceneNodePath.new(data.spawner_path)
	url = data.url
	peer_id = data.peer_id
