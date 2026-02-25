extends CanvasLayer

@export var client: ClientComponent
@onready var connect_to_server: ConnectToServerUI = %"Connect To Server"


func _ready() -> void:
	connect_to_server.player_scene = client.owner.scene_file_path


func _on_connect_player(client_data: MultiplayerClientData) -> void:
	assert(get_tree().current_scene is MultiplayerNetwork)
	var network: MultiplayerNetwork = get_tree().current_scene
	network.connect_player(client_data)
