extends CanvasLayer

@onready var server_ip_edit: TextEdit = %ServerIpEdit
@onready var username_edit: TextEdit = %UsernameEdit


@export_file var menu_file: String


func _on_join_button_pressed() -> void:	
	var username := username_edit.text
	var scene_path := owner.owner.scene_file_path
	var url := server_ip_edit.text
	
	var client_data := MultiplayerClientData.new()
	client_data.username = username
	client_data.scene_path = scene_path
	client_data.url = url

	assert(get_tree().current_scene is MultiplayerNetwork)
	var network: MultiplayerNetwork = get_tree().current_scene
	network.configure(client_data)
