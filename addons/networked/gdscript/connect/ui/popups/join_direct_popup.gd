## Modal form for clients to connect directly to an address.
##
## Renders one field per key of the [code]client_settings[/code] entry of
## [method NetwConnectHandle.transport] for the picked backend, because a
## client reaching a room needs the same tracker and relay the host used.
class_name JoinDirectPopup
extends PopupPanel

signal submitted(
		peer_class: StringName,
		address: String,
		display_name: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array,
)

var _connection: NetwConnectHandle
var _transports: Array[Dictionary] = []
var _settings := ConnectFieldList.new()
var _join_args := ConnectFieldList.new()

@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _address_edit: LineEdit = %AddressEdit
@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _settings_container: VBoxContainer = %SettingsContainer
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


## Opens the direct join popup for [param handle].
func open_join_direct(
		handle: NetwConnectHandle,
		default_username: String,
) -> void:
	_connection = handle
	_address_edit.text = ""
	_username_edit.text = default_username
	_populate_backend_picker()
	_populate_join_args()
	popup_centered()


## Selects [param peer_class] and fills the address with [param address],
## leaving the player only their name to confirm.
##
## A shared link names an endpoint and nothing else, so the form it opens
## arrives already pointed at it.
func preset(peer_class: StringName, address: String) -> void:
	for at in _transports.size():
		if StringName(_transports[at].get("peer_class", &"")) != peer_class:
			continue
		_backend_picker.selected = at
		_refresh_address_hint()
		_populate_settings()
		break
	_address_edit.text = address


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	_transports.clear()
	if _connection != null:
		for transport: Dictionary in _connection.transports():
			var caps := int(transport.get("capabilities", 0))
			if caps & NetwMultiplayer.TRANSPORT_AVAILABLE == 0:
				continue
			_transports.append(transport)
			_backend_picker.add_item(String(transport.get("display_name", "")))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0
	_refresh_address_hint()
	_populate_settings()


func _selected_transport() -> Dictionary:
	if _transports.is_empty():
		return { }
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _transports.size():
		return { }
	return _transports[idx]


func _on_backend_changed(_index: int) -> void:
	_refresh_address_hint()
	_populate_settings()


func _populate_settings() -> void:
	var transport := _selected_transport()
	_settings.render_settings(
		_settings_container,
		transport.get("client_settings", { }),
	)


func _refresh_address_hint() -> void:
	var transport := _selected_transport()
	if _connection == null or transport.is_empty():
		_address_edit.placeholder_text = ""
		_address_edit.tooltip_text = ""
		return
	_address_edit.placeholder_text = String(
		transport.get("address_placeholder", ""),
	)
	_address_edit.tooltip_text = String(transport.get("address_help", ""))


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
	var address := _address_edit.text
	var display_name := address.strip_edges()
	if display_name.is_empty():
		display_name = "-"

	var typed := _username_edit.text.strip_edges()
	var username := StringName(typed) if not typed.is_empty() else &"Player"

	hide()
	submitted.emit(
		peer_class,
		address,
		display_name,
		_settings.as_dictionary(),
		username,
		_join_args.as_array(),
	)
