class_name ConnectToServerUI
extends MarginContainer

signal connect_player(client_data: MultiplayerClientData)

@onready var server_ip_edit: TextEdit = %ServerIpEdit
@onready var username_edit: TextEdit = %UsernameEdit

@export_file var player_scene: String


func _on_join_button_pressed() -> void:	
	assert(not player_scene.is_empty())
	var username := username_edit.text
	var scene_path := player_scene
	var url := server_ip_edit.text
	
	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.scene_path = player_scene
	client_data.url = url
	
	connect_player.emit(client_data)
