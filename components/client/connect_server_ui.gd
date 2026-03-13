extends CanvasLayer

signal connect_player(client_data: MultiplayerClientData)

func _on_connect_player(client_data: MultiplayerClientData) -> void:
	connect_player.emit(client_data)
