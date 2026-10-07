## The address another player types to join this session, with a button to
## copy it.
##
## The bar shows while [member NetwConnection.join_address] is not empty. A
## game that does not want it hides or removes the node.
extends Control

@onready var connection: NetwConnection = Netw.session(self).connection
@onready var panel: Control = %Panel
@onready var code_edit: LineEdit = %CodeEdit


func _ready() -> void:
	connection.join_address_changed.connect(on_join_address_changed)
	%CopyButton.pressed.connect(on_copy_pressed)
	on_join_address_changed(connection.join_address)


func on_join_address_changed(address: String) -> void:
	code_edit.text = address
	panel.visible = not address.is_empty()


func on_copy_pressed() -> void:
	DisplayServer.clipboard_set(code_edit.text)
