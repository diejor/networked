## A form to host a game through one of the session's transports.
class_name HostPopup
extends PopupPanel

signal submitted(
		transport: StringName,
		settings: Dictionary,
		info: NetwServerInfo,
		username: String,
		join_args: Array,
)

var transports: Array[Dictionary] = []
var settings := ConnectFieldList.new()
var join_args := ConnectFieldList.new()

@onready var transport_picker: OptionButton = %BackendPicker
@onready var settings_container: VBoxContainer = %SettingsContainer
@onready var args_container: VBoxContainer = %ArgsContainer
@onready var name_edit: LineEdit = %NameEdit
@onready var username_edit: LineEdit = %UsernameEdit
@onready var confirm_button: Button = %ConfirmButton
@onready var cancel_button: Button = %CancelButton


func _ready() -> void:
	hide()
	transport_picker.item_selected.connect(on_transport_selected)
	confirm_button.pressed.connect(on_confirm_pressed)
	cancel_button.pressed.connect(hide)


func open(connection: NetwConnection, username: String) -> void:
	var hostable := (
			NetwConnection.TRANSPORT_AVAILABLE
			| NetwConnection.TRANSPORT_CAN_HOST
	)
	transports.clear()
	transport_picker.clear()
	for transport in connection.transports():
		if transport.capabilities & hostable == hostable:
			transports.append(transport)
			transport_picker.add_item(transport.display_name)
	name_edit.text = ""
	username_edit.text = username
	join_args.render_schema(args_container, connection.join_schema())
	confirm_button.disabled = transports.is_empty()
	if not transports.is_empty():
		on_transport_selected(0)
	popup_centered()


func on_transport_selected(index: int) -> void:
	settings.render_settings(
		settings_container,
		transports[index].host_settings,
	)


func on_confirm_pressed() -> void:
	var chosen := settings.as_dictionary()
	var info := NetwServerInfo.new()
	info.motd = (
			name_edit.text if not name_edit.text.is_empty()
			else name_edit.placeholder_text
	)
	info.max_players = chosen.get("max_players", 0)
	hide()
	submitted.emit(
		transports[transport_picker.selected].peer_class,
		chosen,
		info,
		(
				username_edit.text if not username_edit.text.is_empty()
				else username_edit.placeholder_text
		),
		join_args.as_array(),
	)
