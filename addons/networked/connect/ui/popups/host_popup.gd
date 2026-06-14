## Modal form for hosting multiplayer sessions.
class_name HostPopup
extends PopupPanel

signal submitted(config: ConnectHostConfig, payload: JoinPayload)

var _templates: Array[BackendPeer] = []
var _spawner_options: Array[SceneNodePath] = []

@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _name_edit: LineEdit = %NameEdit
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


## Opens the host form with pre-populated backend and username details.
func open_host(
		templates: Array[BackendPeer],
		spawner_options: Array[SceneNodePath],
		default_username: String,
) -> void:
	_templates = templates
	_spawner_options = spawner_options.duplicate()
	_name_edit.text = ""
	_username_edit.text = default_username
	_populate_backend_picker()
	_populate_spawner_picker()
	_spawner_row.visible = not _spawner_options.is_empty()
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


func _on_confirm() -> void:
	var template := _selected_template()
	if template == null:
		return
	var config := ConnectHostConfig.new()
	config.backend = template
	config.server_name = _name_edit.text

	var payload := JoinPayload.new()
	var typed := _username_edit.text.strip_edges()
	payload.username = StringName(typed) if not typed.is_empty() else &"Player"
	var spawner := _selected_spawner()
	if spawner != null:
		payload.spawn = EntitySpawnPolicy.from_scene_node_path(spawner).to_dict()

	hide()
	submitted.emit(config, payload)
