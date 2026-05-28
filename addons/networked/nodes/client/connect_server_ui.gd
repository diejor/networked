## Overlay that stamps a spawner path onto a [JoinPayload] from a child
## [ConnectToServerUI] and re-emits the address + payload.
##
## Wire [signal connect_requested] to a callback that drives
## [method MultiplayerTree.join_direct] or
## [method MultiplayerTree.auto_connect_player] with the desired backend.
extends CanvasLayer

## Emitted when the player submits connection details.
signal connect_requested(server_address: String, join_payload: JoinPayload)

## [SceneNodePath] pointing to the target [SpawnerComponent] spawner.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:SpawnerComponent")
var spawner_node: SceneNodePath

func _ready() -> void:
	if not spawner_node:
		queue_free()

func _on_connect_requested(
	server_address: String, join_payload: JoinPayload
) -> void:
	assert(spawner_node.is_valid(), "Spawner must be valid to connect.")
	join_payload.spawner_component_path = spawner_node
	connect_requested.emit(server_address, join_payload)
