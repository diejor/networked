class_name ConnectToServerUI
extends MarginContainer

signal connect_player(client_data: MultiplayerClientData)

@onready var server_ip_edit: TextEdit = %ServerIpEdit
@onready var username_edit: TextEdit = %UsernameEdit


func _on_join_button_pressed() -> void:	
	var username := username_edit.text
	var url := server_ip_edit.text
	
	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.url = url
	
	connect_player.emit(client_data)
