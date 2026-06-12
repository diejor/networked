## Modal form for clients to connect directly to an IP address.
class_name JoinDirectPopup
extends PopupPanel

signal submitted(target: JoinTarget, payload: JoinPayload)

var _templates: Array[BackendPeer] = []
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
	_backend_picker.item_selected.connect(_on_backend_changed)


## Opens the direct join popup.
func open_join_direct(
		templates: Array[BackendPeer],
		spawner_options: Array[SceneNodePath],
		default_username: String,
) -> void:
	_templates = templates
	_spawner_options = spawner_options.duplicate()
	_address_edit.text = ""
	_username_edit.text = default_username
	_populate_backend_picker()
	_populate_spawner_picker()
	_spawner_row.visible = not _spawner_options.is_empty()
	_refresh_address_hint()
	popup_centered()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	for backend in _templates:
		_backend_picker.add_item(ConnectUiShared.format_backend_label(backend))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0


func _populate_spawner_picker() -> void:
	_spawner_picker.clear()
	for path in _spawner_options:
		_spawner_picker.add_item(ConnectUiShared.format_spawner_label(path))
	if _spawner_picker.item_count > 0:
		_spawner_picker.selected = 0


func _selected_template() -> BackendPeer:
	if _templates.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _templates.size():
		return null
	return _templates[idx]


func _selected_spawner() -> SceneNodePath:
	if _spawner_options.is_empty():
		return null
	var idx := maxi(0, _spawner_picker.selected)
	if idx >= _spawner_options.size():
		return null
	return _spawner_options[idx]


func _on_backend_changed(_index: int) -> void:
	_refresh_address_hint()


func _refresh_address_hint() -> void:
	var template := _selected_template()
	if template == null:
		_address_edit.placeholder_text = ""
		_address_edit.tooltip_text = ""
		return
	var hint := template.get_address_hint()
	_address_edit.placeholder_text = hint.placeholder
	_address_edit.tooltip_text = hint.help_text


func _on_confirm() -> void:
	var template := _selected_template()
	if template == null:
		return
	var target := JoinTarget.new()
	target.address = _address_edit.text
	target.backend = template
	target.display_name = ConnectUiShared.format_address(target)

	var payload := JoinPayload.new()
	var typed := _username_edit.text.strip_edges()
	payload.username = StringName(typed) if not typed.is_empty() else &"Player"
	var spawner := _selected_spawner()
	if spawner != null:
		payload.spawn = EntitySpawnPolicy.from_scene_node_path(spawner).to_dict()

	hide()
	submitted.emit(target, payload)
