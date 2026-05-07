## Overlay UI that relays a [JoinPayload] from a child [ConnectToServerUI] to a [MultiplayerTree].
##
## Wire [signal connect_player] to [method MultiplayerTree.connect_player], then assign
## [member spawner_node] so that the spawner path is stamped into the client data automatically.
extends CanvasLayer

## Emitted when the player submits connection details. Connect to [method MultiplayerTree.connect_player].
signal connect_player(join_payload: JoinPayload)

## [SceneNodePath] pointing to the target [SpawnerPlayerComponent] spawner.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:SpawnerPlayerComponent")
var spawner_node: SceneNodePath

func _ready() -> void:
	if not spawner_node:
		queue_free()

func _on_connect_player(join_payload: JoinPayload) -> void:
	assert(spawner_node.is_valid(), "Spawner must be valid to connect.")
	join_payload.spawner_component_path = spawner_node
	connect_player.emit(join_payload)
