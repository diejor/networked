extends CanvasLayer

@onready var server_ip_edit: TextEdit = %ServerIpEdit
@onready var username_edit: TextEdit = %UsernameEdit
var _hide_on_connect := false

@export_file var menu_file: String

func _ready() -> void:
	if "--server" in OS.get_cmdline_args():
		visible = false


func on_connected_to_server() -> void:
	if _hide_on_connect:
		visible = false


func _on_join_button_pressed() -> void:
	var server_address := server_ip_edit.text
	var username := username_edit.text
	_hide_on_connect = true
	Server.backend.peer_reset_state()
	var err: Error = await Client.connect_client(server_address, username)
	if err != OK:
		push_warning("Connection failed: %s" % error_string(err))
		_hide_on_connect = false
	get_tree().change_scene_to_file(menu_file)
