## Modal form for the four flows the [ConnectBrowser] needs: adding
## or editing a saved [JoinTarget], hosting a session, and joining a
## selected target. Each flow toggles a subset of the form rows, and the
## right one fires a typed signal on confirm.
class_name ConnectPopup
extends PopupPanel


enum Form { ADD, EDIT, HOST, JOIN }


## Emitted in ADD or EDIT mode. [param persist] reflects the "Save
## to server list" checkbox.
signal target_submitted(target: JoinTarget, persist: bool)

## Emitted in HOST mode. The same [JoinPayload] shape carries the
## hosting player's identity.
signal host_submitted(config: ConnectHostConfig, payload: JoinPayload)

## Emitted in JOIN mode for the target supplied to [method open_join].
signal join_submitted(payload: JoinPayload)

## Emitted when the user cancels.
signal cancelled


class _HostChoice extends RefCounted:
	var direct_template: BackendPeer
	var provider_id: StringName

	func is_direct() -> bool:
		return provider_id == &""


var _mode: Form = Form.ADD
var _templates: Array[BackendPeer] = []
var _host_choices: Array[_HostChoice] = []
var _spawner_options: Array[SceneNodePath] = []
var _editing: JoinTarget = null
var _pending_target: JoinTarget = null


@onready var _title: Label = %TitleLabel
@onready var _backend_row: HBoxContainer = %BackendRow
@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _choice_row: HBoxContainer = %ChoiceRow
@onready var _choice_picker: OptionButton = %ChoicePicker
@onready var _address_row: HBoxContainer = %AddressRow
@onready var _address_edit: LineEdit = %AddressEdit
@onready var _name_row: HBoxContainer = %NameRow
@onready var _name_label: Label = %NameLabel
@onready var _name_edit: LineEdit = %NameEdit
@onready var _save_check: CheckBox = %SaveCheck
@onready var _username_row: HBoxContainer = %UsernameRow
@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _spawner_row: HBoxContainer = %SpawnerRow
@onready var _spawner_picker: OptionButton = %SpawnerPicker
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(_on_cancel)
	_backend_picker.item_selected.connect(_on_backend_changed)


## Sets the backend templates offered in ADD/EDIT and HOST modes.
func set_templates(templates: Array[BackendPeer]) -> void:
	_templates = templates


## Sets the spawner choices shown in HOST/JOIN modes. Hidden when
## empty.
func set_spawner_options(options: Array[SceneNodePath]) -> void:
	_spawner_options = options.duplicate()


## Opens the popup as an empty Add Server form.
func open_add() -> void:
	_mode = Form.ADD
	_editing = null
	_show_target_mode("Add server", "Add")
	_save_check.button_pressed = true
	_address_edit.text = ""
	_name_edit.text = ""
	_populate_backend_picker()
	_refresh_address_hint()
	popup_centered()


## Opens the popup as an Edit form populated from [param target].
func open_edit(target: JoinTarget) -> void:
	_mode = Form.EDIT
	_editing = target
	_show_target_mode("Edit server", "Save")
	_save_check.button_pressed = true
	_address_edit.text = target.address
	_name_edit.text = target.display_name
	_populate_backend_picker()
	_select_template_for(target.backend)
	_refresh_address_hint()
	popup_centered()


## Opens the popup as a Host form with [param templates] and
## [param provider_ids] in the choice picker, where [param default_username]
## seeds the username field.
func open_host(
	templates: Array[BackendPeer],
	provider_ids: Array[StringName],
	default_username: String,
) -> void:
	_mode = Form.HOST
	_show_host_mode()
	_name_edit.text = ""
	_username_edit.text = default_username
	_populate_host_choices(templates, provider_ids)
	_populate_spawner_picker()
	popup_centered()


## Opens the popup as a Join form for [param target] with
## [param default_username] seeded in the username field.
func open_join(target: JoinTarget, default_username: String) -> void:
	_mode = Form.JOIN
	_pending_target = target
	_show_join_mode()
	_username_edit.text = default_username
	_populate_spawner_picker()
	popup_centered()


func _show_target_mode(title: String, confirm: String) -> void:
	_title.text = title
	_confirm_button.text = confirm
	_backend_row.visible = true
	_choice_row.visible = false
	_address_row.visible = true
	_name_row.visible = true
	_name_label.text = "Display name"
	_save_check.visible = true
	_username_row.visible = false
	_spawner_row.visible = false


func _show_host_mode() -> void:
	_title.text = "Host server"
	_confirm_button.text = "Host"
	_backend_row.visible = false
	_choice_row.visible = true
	_address_row.visible = false
	_name_row.visible = true
	_name_label.text = "Server name"
	_save_check.visible = false
	_username_row.visible = true
	_spawner_row.visible = not _spawner_options.is_empty()


func _show_join_mode() -> void:
	_title.text = "Join server"
	_confirm_button.text = "Join"
	_backend_row.visible = false
	_choice_row.visible = false
	_address_row.visible = false
	_name_row.visible = false
	_save_check.visible = false
	_username_row.visible = true
	_spawner_row.visible = not _spawner_options.is_empty()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	for backend in _templates:
		_backend_picker.add_item(ConnectUiShared.format_backend_label(backend))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0


func _populate_host_choices(
	templates: Array[BackendPeer], provider_ids: Array[StringName]
) -> void:
	_host_choices.clear()
	_choice_picker.clear()
	for backend in templates:
		var c := _HostChoice.new()
		c.direct_template = backend
		_host_choices.append(c)
		_choice_picker.add_item(
			"Direct: %s" % ConnectUiShared.format_backend_label(backend)
		)
	for id in provider_ids:
		var c := _HostChoice.new()
		c.provider_id = id
		_host_choices.append(c)
		_choice_picker.add_item("Provider: %s" % String(id).capitalize())
	if _choice_picker.item_count > 0:
		_choice_picker.selected = 0


func _populate_spawner_picker() -> void:
	_spawner_picker.clear()
	for path in _spawner_options:
		_spawner_picker.add_item(ConnectUiShared.format_spawner_label(path))
	if _spawner_picker.item_count > 0:
		_spawner_picker.selected = 0


func _select_template_for(backend: BackendPeer) -> void:
	if backend == null:
		return
	for i in _templates.size():
		if _templates[i].get_class() == backend.get_class():
			_backend_picker.selected = i
			return


func _selected_template() -> BackendPeer:
	if _templates.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _templates.size():
		return null
	return _templates[idx]


func _selected_host_choice() -> _HostChoice:
	if _host_choices.is_empty():
		return null
	var idx := maxi(0, _choice_picker.selected)
	if idx >= _host_choices.size():
		return null
	return _host_choices[idx]


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
	if not _address_row.visible:
		return
	var template := _selected_template()
	if template == null:
		_address_edit.placeholder_text = ""
		_address_edit.tooltip_text = ""
		return
	var hint := template.get_address_hint()
	_address_edit.placeholder_text = hint.placeholder
	_address_edit.tooltip_text = hint.help_text


func _on_confirm() -> void:
	match _mode:
		Form.ADD, Form.EDIT:
			var template := _selected_template()
			if template == null:
				return
			var target: JoinTarget = _editing if _editing else JoinTarget.new()
			target.address = _address_edit.text
			target.backend = template
			target.display_name = (
				_name_edit.text if not _name_edit.text.is_empty()
				else ConnectUiShared.format_address(target)
			)
			hide()
			target_submitted.emit(target, _save_check.button_pressed)
		Form.HOST:
			var choice := _selected_host_choice()
			if choice == null:
				return
			var config := ConnectHostConfig.new()
			if choice.is_direct():
				config.backend = choice.direct_template
			else:
				config.provider_id = choice.provider_id
			config.server_name = _name_edit.text
			hide()
			host_submitted.emit(config, _build_payload())
		Form.JOIN:
			hide()
			join_submitted.emit(_build_payload())


func _on_cancel() -> void:
	hide()
	cancelled.emit()


func _build_payload() -> JoinPayload:
	var payload := JoinPayload.new()
	var typed := _username_edit.text.strip_edges()
	payload.username = StringName(typed) if not typed.is_empty() else &"Player"
	var spawner := _selected_spawner()
	if spawner != null:
		payload.spawner_component_path = spawner
	return payload
