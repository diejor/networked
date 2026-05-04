## Simple form UI with server IP and username fields that emits connection data.
##
## Place inside a [code]connect_server_ui.gd[/code] scene. Connect [signal connect_player]
## to the parent overlay's forwarding signal.
class_name ConnectToServerUI
extends MarginContainer

## Emitted when the join button is pressed, carrying the filled-in [JoinPayload].
signal connect_player(join_payload: JoinPayload)

@onready var username_edit: TextEdit = %UsernameEdit
@onready var server_ip_edit: TextEdit = %ServerIpEdit


func _on_join_button_pressed() -> void:	
	var username := username_edit.text
	var url := server_ip_edit.text
	
	var join_payload := JoinPayload.new()
	join_payload.username = username
	join_payload.url = url
	
	connect_player.emit(join_payload)
