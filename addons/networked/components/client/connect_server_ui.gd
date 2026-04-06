## Overlay UI that relays a [MultiplayerClientData] from a child [ConnectToServerUI] to a [NetworkSession].
##
## Wire [signal connect_player] to [method NetworkSession.connect_player], then assign
## [member spawner_node] so that the spawner path is stamped into the client data automatically.
extends CanvasLayer

## Emitted when the player submits connection details. Connect to [method NetworkSession.connect_player].
signal connect_player(client_data: MultiplayerClientData)

## [SceneNodePath] pointing to the target [ClientComponent] spawner.
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:ClientComponent")
var spawner_node: SceneNodePath

func _ready() -> void:
	if not spawner_node:
		queue_free()

func _on_connect_player(client_data: MultiplayerClientData) -> void:
	assert(spawner_node.is_valid(), "Spawner must be valid to connect.")
	client_data.spawner_path = spawner_node
	connect_player.emit(client_data)
