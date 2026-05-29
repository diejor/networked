## Modal form for adding or editing a direct [JoinTarget].
##
## Configure the available backends with [method set_templates] before
## calling [method open_add] or [method open_edit]. The result is
## delivered via [signal submitted]; cancellation simply hides the
## popup without emitting.
@tool
class_name ServerBrowserPopup
extends PopupPanel


## Emitted when the user confirms the form. [param target] is a fresh
## [JoinTarget] (Add mode) or the same instance passed to
## [method open_edit] mutated in place. [param persist] reflects the
## "Save to server list" checkbox.
signal submitted(target: JoinTarget, persist: bool)


var _editing: JoinTarget
var _templates: Array[BackendPeer] = []


var _title_label: Label
var _backend_picker: OptionButton
var _address_edit: LineEdit
var _name_edit: LineEdit
var _save_check: CheckBox
var _confirm_button: Button
var _cancel_button: Button


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	_title_label = %TitleLabel
	_backend_picker = %BackendPicker
	_address_edit = %AddressEdit
	_name_edit = %NameEdit
	_save_check = %SaveCheck
	_confirm_button = %ConfirmButton
	_cancel_button = %CancelButton

	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(_on_cancel)
	_backend_picker.item_selected.connect(_on_backend_changed)


## Sets the backend templates the picker offers. Call before opening
## the popup.
func set_templates(templates: Array[BackendPeer]) -> void:
	if Engine.is_editor_hint():
		return
	_templates = templates
	if _backend_picker == null:
		return
	_backend_picker.clear()
	for backend in _templates:
		_backend_picker.add_item(_template_label(backend))


## Opens the popup in Add mode with empty fields.
func open_add() -> void:
	if Engine.is_editor_hint():
		return
	_editing = null
	_title_label.text = "Add server"
	_confirm_button.text = "Add"
	_address_edit.text = ""
	_name_edit.text = ""
	_save_check.button_pressed = true
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0
	_refresh_hint()
	popup_centered()


## Opens the popup in Edit mode populated from [param target].
func open_edit(target: JoinTarget) -> void:
	if Engine.is_editor_hint():
		return
	_editing = target
	_title_label.text = "Edit server"
	_confirm_button.text = "Save"
	_address_edit.text = target.address
	_name_edit.text = target.display_name
	_save_check.button_pressed = true
	_select_template_for(target.backend)
	_refresh_hint()
	popup_centered()


func _select_template_for(backend: BackendPeer) -> void:
	if backend == null:
		return
	for i in _templates.size():
		if _templates[i].get_class() == backend.get_class():
			_backend_picker.selected = i
			return


func _on_backend_changed(_index: int) -> void:
	_refresh_hint()


func _refresh_hint() -> void:
	var template := _selected_template()
	if template == null:
		_address_edit.placeholder_text = ""
		return
	var hint := template.get_address_hint()
	_address_edit.placeholder_text = hint.placeholder
	_address_edit.tooltip_text = hint.help_text


func _selected_template() -> BackendPeer:
	if _templates.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _templates.size():
		return null
	return _templates[idx]


func _template_label(backend: BackendPeer) -> String:
	if backend == null:
		return "-"
	if backend.resource_path.is_empty() or "::" in backend.resource_path:
		return _backend_class_name(backend)
	return backend.resource_path.get_file()


func _backend_class_name(backend: BackendPeer) -> String:
	var script := backend.get_script()
	if script and not script.get_global_name().is_empty():
		return script.get_global_name()
	return backend.get_class()


func _on_confirm() -> void:
	var template := _selected_template()
	if template == null:
		return
	var target: JoinTarget = _editing if _editing else JoinTarget.new()
	target.address = _address_edit.text
	target.backend = template
	target.display_name = (
		_name_edit.text if not _name_edit.text.is_empty()
		else _display_address(target)
	)
	hide()
	submitted.emit(target, _save_check.button_pressed)


func _on_cancel() -> void:
	hide()


func _display_address(target: JoinTarget) -> String:
	var address := target.address.strip_edges()
	if not address.is_empty():
		return address
	if target.backend == null:
		return "-"
	if target.backend.has_method("build_url"):
		return str(target.backend.call("build_url", ""))
	return target.backend.get_join_address()
