## Minimal connect form. Replaces the deleted [code]ConnectToServer[/code]
## scene with an example that speaks the [JoinTarget] vocabulary.
##
## Wire [signal connect_requested] to a callback that drives
## [method MultiplayerTree.auto_connect_player] or
## [method MultiplayerTree.join_direct]. Set
## [member backend_templates] to the backends the user can pick from;
## the dropdown is hidden when only one is supplied.
@tool
class_name ConnectOverlay
extends Control


## Emitted when the user clicks Connect. The returned [JoinTarget] is
## always a direct target ([code]provider_id[/code] is empty).
signal connect_requested(target: JoinTarget)


## Backends offered in the picker. The first entry is the default
## selection.
@export var backend_templates: Array[BackendPeer] = []

## Spawner candidates the user picks from. Stamped onto
## [member JoinPayload.spawner_component_path] when the chosen
## [JoinTarget] is emitted. Empty means no spawner is set.
@export_custom(
	PROPERTY_HINT_ARRAY_TYPE,
	"24/17:SceneNodePath:SpawnerComponent",
)
var spawner_options: Array[SceneNodePath] = []


@onready var _address_edit: LineEdit = %AddressEdit
@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _backend_label: Label = %BackendLabel
@onready var _spawner_picker: OptionButton = %SpawnerPicker
@onready var _spawner_label: Label = %SpawnerLabel
@onready var _connect_button: Button = %ConnectButton


func _ready() -> void:
	_populate_backend_picker()
	_populate_spawner_picker()
	_connect_button.pressed.connect(_on_connect_pressed)
	_backend_picker.item_selected.connect(_on_backend_changed)
	_refresh_address_hint()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	if backend_templates.is_empty():
		_backend_picker.add_item("(no backend)")
		_backend_picker.disabled = true
		return
	for backend in backend_templates:
		var name := backend.resource_path.get_file()
		if name.is_empty():
			name = backend.get_class()
		_backend_picker.add_item(name)
	var hide_picker := backend_templates.size() <= 1
	_backend_picker.visible = not hide_picker
	_backend_label.visible = not hide_picker


func _on_backend_changed(_index: int) -> void:
	_refresh_address_hint()


func _refresh_address_hint() -> void:
	var template := _selected_template()
	if template == null:
		_address_edit.placeholder_text = ""
		return
	var hint := template.get_address_hint()
	_address_edit.placeholder_text = hint.placeholder
	_address_edit.tooltip_text = hint.help_text


func _selected_template() -> BackendPeer:
	if backend_templates.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= backend_templates.size():
		return null
	return backend_templates[idx]


func _on_connect_pressed() -> void:
	var template := _selected_template()
	if template == null:
		return
	var target := JoinTarget.new()
	target.display_name = _address_edit.text
	target.address = _address_edit.text
	target.backend = template
	target.metadata["username"] = _username_edit.text
	var spawner := _selected_spawner()
	if spawner != null:
		target.metadata["spawner_component_path"] = spawner
	connect_requested.emit(target)


## Returns the spawner the user picked, or [code]null[/code] when
## [member spawner_options] is empty.
func get_selected_spawner() -> SceneNodePath:
	return _selected_spawner()


func _populate_spawner_picker() -> void:
	_spawner_picker.clear()
	if spawner_options.is_empty():
		_spawner_picker.visible = false
		_spawner_label.visible = false
		return
	for path in spawner_options:
		_spawner_picker.add_item(_spawner_label_for(path))


func _spawner_label_for(path: SceneNodePath) -> String:
	if path == null:
		return "(none)"
	if path.node_path.is_empty():
		return path.scene_path
	return path.node_path


func _selected_spawner() -> SceneNodePath:
	if spawner_options.is_empty():
		return null
	var idx := maxi(0, _spawner_picker.selected)
	if idx >= spawner_options.size():
		return null
	return spawner_options[idx]


## Returns the username currently entered in the form. The browser
## scene reads it through this getter so the field stays a single
## source of truth.
func get_username() -> String:
	if _username_edit == null:
		return ""
	return _username_edit.text
