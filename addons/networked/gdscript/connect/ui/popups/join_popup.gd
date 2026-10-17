## A form to join a listed server.
class_name JoinPopup
extends PopupPanel

signal submitted(settings: Dictionary, username: String, join_args: Array)

var settings := ConnectFieldList.new()
var join_args := ConnectFieldList.new()

@onready var username_edit: LineEdit = %UsernameEdit
@onready var settings_container: VBoxContainer = %SettingsContainer
@onready var args_container: VBoxContainer = %ArgsContainer
@onready var confirm_button: Button = %ConfirmButton
@onready var cancel_button: Button = %CancelButton
## Warns that joining leaves the current lobby.
@onready var leave_warning: Label = %LeaveWarning


func _ready() -> void:
	hide()
	confirm_button.pressed.connect(on_confirm_pressed)
	cancel_button.pressed.connect(hide)


func open(
		connection: NetwConnection,
		endpoint: NetwEndpoint,
		username: String,
) -> void:
	leave_warning.visible = connection.state == NetwConnection.STATE_ONLINE
	username_edit.text = username
	settings.render_settings(
		settings_container,
		connection.transport(endpoint.peer_class).client_settings,
	)
	join_args.render_schema(args_container, connection.join_schema())
	popup_centered()


func on_confirm_pressed() -> void:
	hide()
	submitted.emit(
		settings.as_dictionary(),
		(
				username_edit.text if not username_edit.text.is_empty()
				else username_edit.placeholder_text
		),
		join_args.as_array(),
	)
