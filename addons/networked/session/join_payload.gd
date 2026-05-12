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

## Set by [method resolve] after successful boundary validation.
## Downstream code uses this directly; never null-checks it.
var resolved: ResolvedJoin


## Validated join data with all fields guaranteed non-null.
##
## Produced by [method JoinPayload.resolve]. Downstream code asserts on
## these values rather than null-checking.
class ResolvedJoin extends RefCounted:
	var peer_id: int
	var username: StringName
	var scene_name: StringName
	var spawner_path: NodePath
	var is_debug: bool


## Validates structural fields and produces a [ResolvedJoin].
##
## Returns [code]null[/code] if [member username] is empty.
## [member spawner_component_path] is optional -- when set, its fields
## are unpacked into [ResolvedJoin]; when absent, [member ResolvedJoin.scene_name]
## and [member ResolvedJoin.spawner_path] remain empty.
func resolve() -> ResolvedJoin:
	if username.is_empty():
		return null
	var r := ResolvedJoin.new()
	r.peer_id = peer_id
	r.username = username
	r.is_debug = is_debug
	if spawner_component_path and spawner_component_path.is_valid():
		r.scene_name = StringName(spawner_component_path.get_scene_name())
		r.spawner_path = spawner_component_path.node_path
	resolved = r
	return r


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
	return var_to_bytes(dict)


## Populates this object from a serialized [PackedByteArray] produced by
## [method serialize].
func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)

	username = data.username
	spawner_component_path = SceneNodePath.new(data.spawner_component_path)
	url = data.url
	peer_id = data.peer_id
	is_debug = data.get("is_debug", false)
