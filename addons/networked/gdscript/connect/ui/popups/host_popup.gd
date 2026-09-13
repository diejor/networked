## Modal form for hosting multiplayer sessions.
##
## Renders one field per key of the [code]host_settings[/code] entry of
## [method NetwConnectHandle.transport] for the picked backend, so it
## authors no per-transport controls of its own.
class_name HostPopup
extends PopupPanel

signal submitted(
		peer_class: StringName,
		settings: Dictionary,
		info: NetwServerInfo,
		username: StringName,
		join_args: Array,
)

var _connection: NetwConnectHandle
var _transports: Array[Dictionary] = []
var _settings := ConnectFieldList.new()
var _join_args := ConnectFieldList.new()

@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _settings_container: VBoxContainer = %SettingsContainer
@onready var _name_edit: LineEdit = %NameEdit
@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _args_container: VBoxContainer = %ArgsContainer
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)
	_backend_picker.item_selected.connect(_on_backend_changed)


## Opens the host form for [param handle], offering only the backends it
## reports as available and hostable on this build.
func open_host(
		handle: NetwConnectHandle,
		default_username: String,
) -> void:
	_connection = handle
	_name_edit.text = ""
	_username_edit.text = default_username
	_populate_backend_picker()
	_populate_join_args()
	popup_centered()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	_transports.clear()
	if _connection != null:
		var hostable := (
				NetwMultiplayer.TRANSPORT_AVAILABLE
				| NetwMultiplayer.TRANSPORT_CAN_HOST
		)
		for transport: Dictionary in _connection.transports():
			var caps := int(transport.get("capabilities", 0))
			if caps & hostable != hostable:
				continue
			_transports.append(transport)
			_backend_picker.add_item(String(transport.get("display_name", "")))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0
	_populate_settings()


func _selected_transport() -> Dictionary:
	if _transports.is_empty():
		return { }
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _transports.size():
		return { }
	return _transports[idx]


func _on_backend_changed(_index: int) -> void:
	_populate_settings()


func _populate_settings() -> void:
	var transport := _selected_transport()
	if _connection == null or transport.is_empty():
		_settings.render_settings(_settings_container, { })
		return
	_settings.render_settings(
		_settings_container,
		transport.get("host_settings", { }),
	)


func _populate_join_args() -> void:
	if _connection == null:
		_join_args.render_schema(_args_container, [])
		return
	_join_args.render_schema(_args_container, _connection.join_schema())


func _on_confirm() -> void:
	var transport := _selected_transport()
	if transport.is_empty():
		return
	var peer_class: StringName = transport.get("peer_class")
	var settings := _settings.as_dictionary()
	var typed_name := _name_edit.text.strip_edges()
	var info := NetwServerInfo.new()
	info.motd = (
			typed_name if not typed_name.is_empty()
			else ConnectBrowser.PLACEHOLDER_SERVER_NAME
	)
	info.max_players = int(settings.get("max_players", 0))

	var typed := _username_edit.text.strip_edges()
	var username := StringName(typed) if not typed.is_empty() else &"Player"

	hide()
	submitted.emit(peer_class, settings, info, username, _join_args.as_array())
