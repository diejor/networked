extends CanvasLayer

signal connect_player(client_data: MultiplayerClientData)

@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:ClientComponent")
var spawner_node: SceneNodePath

func _ready() -> void:
	if not spawner_node:
		queue_free()

func _on_connect_player(client_data: MultiplayerClientData) -> void:
	assert(spawner_node.is_valid(), "Spawner must be valid to connect.")
	client_data.spawner_path = spawner_node
	connect_player.emit(client_data)
