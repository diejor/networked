## Serializable data bag describing a player attempting to connect to a session.
##
## Pass a populated instance to [method MultiplayerTree.connect_player] to
## authenticate and spawn a player, or serialize it for transmission via
## [method serialize].
class_name JoinPayload
extends Serde

## The player's display name, used as the spawned node name prefix.
@export var username: StringName

## Path to the [SpawnerComponent] node the player should enter.
##
## [b]Note:[/b] The target [SpawnerComponent] must reside in a scene that
## tracks the owner scene correctly.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:SpawnerComponent")
var spawner_component_path: SceneNodePath

## Optional path to a [MultiplayerSpawner] that will receive the spawn
## payload instead of a [SpawnerComponent].
##
## When set, the framework calls [method MultiplayerSpawner.spawn] with
## the gathered payload after activating the target scene.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:MultiplayerSpawner")
var multiplayer_spawner_path: SceneNodePath

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


## Serializes the join payload into a [PackedByteArray] for network
## transmission.
func serialize() -> PackedByteArray:
	var dict: Dictionary = {
		username = username,
		spawner_component_path = (
			spawner_component_path.as_uid() if spawner_component_path else ""
		),
		url = url,
		peer_id = peer_id,
		is_debug = is_debug,
	}
	if multiplayer_spawner_path and multiplayer_spawner_path.is_valid():
		dict.multiplayer_spawner_path = multiplayer_spawner_path.as_uid()
	return var_to_bytes(dict)


## Populates this object from a serialized [PackedByteArray] produced by
## [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)

	username = data.username
	spawner_component_path = SceneNodePath.new(data.spawner_component_path)
	if data.get("multiplayer_spawner_path"):
		multiplayer_spawner_path = SceneNodePath.new(
			data.multiplayer_spawner_path
		)
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
