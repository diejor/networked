## Simple form UI emitting a username, a server address, and a [JoinPayload].
##
## Place inside a [code]connect_server_ui.gd[/code] scene. The parent overlay
## passes the emitted address + payload to
## [method MultiplayerTree.join_direct] or
## [method MultiplayerTree.auto_connect_player] with the desired backend.
class_name ConnectToServerUI
extends MarginContainer

## Emitted when the join button is pressed.
signal connect_requested(server_address: String, join_payload: JoinPayload)

@onready var username_edit: TextEdit = %UsernameEdit
@onready var server_ip_edit: TextEdit = %ServerIpEdit


func _on_join_button_pressed() -> void:
	var join_payload := JoinPayload.new()
	join_payload.username = username_edit.text
	connect_requested.emit(server_ip_edit.text, join_payload)
