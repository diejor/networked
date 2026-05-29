## Modal form for per-join options.
##
## Configure spawn choices with [method set_spawner_options] before
## opening. Confirmation fires [signal submitted]; cancellation hides
## the popup without emitting.
@tool
class_name ServerBrowserJoinPopup
extends PopupPanel


## Emitted when the user confirms the join options.
signal submitted(username: String, spawner: SceneNodePath)


var _spawner_options: Array[SceneNodePath] = []


var _username_edit: LineEdit
var _title: Label
var _spawner_row: HBoxContainer
var _spawner_picker: OptionButton
var _confirm_button: Button
var _cancel_button: Button


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_username_edit = %UsernameEdit
	_title = %Title
	_spawner_row = %SpawnerRow
	_spawner_picker = %SpawnerPicker
	_confirm_button = %ConfirmButton
	_cancel_button = %CancelButton

	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)


## Sets the spawn locations shown in the popup.
func set_spawner_options(options: Array[SceneNodePath]) -> void:
	if Engine.is_editor_hint():
		return
	_spawner_options = options.duplicate()
	_spawner_picker.clear()
	if _spawner_options.is_empty():
		_spawner_row.visible = false
		return
	for path in _spawner_options:
		_spawner_picker.add_item(_spawner_label_for(path))
	_spawner_row.visible = true


## Shows the popup using [param username] as the initial player name.
func open(
	username: String = "",
	title: String = "Join server",
	confirm_text: String = "Join",
) -> void:
	if Engine.is_editor_hint():
		return
	_title.text = title
	_confirm_button.text = confirm_text
	_username_edit.text = username
	if _spawner_picker.item_count > 0:
		_spawner_picker.selected = 0
	popup_centered()


func _on_confirm() -> void:
	hide()
	submitted.emit(_username_edit.text, _selected_spawner())


func _selected_spawner() -> SceneNodePath:
	if _spawner_options.is_empty():
		return null
	var idx := maxi(0, _spawner_picker.selected)
	if idx >= _spawner_options.size():
		return null
	return _spawner_options[idx]


func _spawner_label_for(path: SceneNodePath) -> String:
	if path == null:
		return "(none)"
	if path.node_path.is_empty():
		return path.scene_path
	return path.node_path
