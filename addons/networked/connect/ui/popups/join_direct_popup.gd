## Modal form for clients to connect directly to an IP address.
class_name JoinDirectPopup
extends PopupPanel

signal submitted(target: NetwConnectTarget, payload: JoinPayload)

var _transports: Array[NetwTransport] = []
var _spawner_options: Array[SceneNodePath] = []

@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _address_edit: LineEdit = %AddressEdit
@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _spawner_row: HBoxContainer = %SpawnerRow
@onready var _spawner_picker: OptionButton = %SpawnerPicker
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)
	_backend_picker.item_selected.connect(_on_transport_changed)


## Opens the direct join popup.
func open_join_direct(
		transports: Array[NetwTransport],
		spawner_options: Array[SceneNodePath],
		default_username: String,
) -> void:
	_transports = transports
	_spawner_options = spawner_options.duplicate()
	_address_edit.text = ""
	_username_edit.text = default_username
	_populate_transport_picker()
	_populate_spawner_picker()
	_spawner_row.visible = not _spawner_options.is_empty()
	_refresh_address_hint()
	popup_centered()


func _populate_transport_picker() -> void:
	_backend_picker.clear()
	for transport in _transports:
		_backend_picker.add_item(transport._display_name())
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0


func _populate_spawner_picker() -> void:
	_spawner_picker.clear()
	for path in _spawner_options:
		_spawner_picker.add_item(ConnectBrowser.format_spawner_label(path))
	if _spawner_picker.item_count > 0:
		_spawner_picker.selected = 0


func _selected_transport() -> NetwTransport:
	if _transports.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _transports.size():
		return null
	return _transports[idx]


func _selected_spawner() -> SceneNodePath:
	if _spawner_options.is_empty():
		return null
	var idx := maxi(0, _spawner_picker.selected)
	if idx >= _spawner_options.size():
		return null
	return _spawner_options[idx]


func _on_transport_changed(_index: int) -> void:
	_refresh_address_hint()


func _refresh_address_hint() -> void:
	var transport := _selected_transport()
	if transport == null:
		_address_edit.placeholder_text = ""
		_address_edit.tooltip_text = ""
		return
	var hint := transport._address_hint()
	_address_edit.placeholder_text = hint.placeholder
	_address_edit.tooltip_text = hint.help_text


func _on_confirm() -> void:
	var transport := _selected_transport()
	if transport == null:
		return
	var target := NetwConnectTarget.new()
	target.scheme = transport.scheme()
	target.address = _address_edit.text
	target.display_name = ConnectBrowser.format_address(target)

	var payload := JoinPayload.new()
	var typed := _username_edit.text.strip_edges()
	payload.username = StringName(typed) if not typed.is_empty() else &"Player"
	var spawner := _selected_spawner()
	if spawner != null:
		payload.arg_values = NetwDefaultJoin.args_from_scene_node_path(spawner)

	hide()
	submitted.emit(target, payload)
