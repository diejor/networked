## Modal form for adding and editing targets in the [ConnectBrowser].
class_name AddPopup
extends PopupPanel

signal submitted(target: JoinTarget)

var _templates: Array[BackendPeer] = []
var _editing: JoinTarget = null

@onready var _title: Label = %TitleLabel
@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _address_edit: LineEdit = %AddressEdit
@onready var _name_edit: LineEdit = %NameEdit
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)
	_backend_picker.item_selected.connect(_on_backend_changed)


## Sets the backend templates offered in the picker.
func set_templates(templates: Array[BackendPeer]) -> void:
	_templates = templates


## Opens the popup as an empty Add Server form.
func open_add() -> void:
	_editing = null
	_title.text = "Add server"
	_confirm_button.text = "Add"
	_address_edit.text = ""
	_name_edit.text = ""
	_populate_backend_picker()
	_refresh_address_hint()
	popup_centered()


## Opens the popup as an Edit form populated from [param target].
func open_edit(target: JoinTarget) -> void:
	_editing = target
	_title.text = "Edit server"
	_confirm_button.text = "Save"
	_address_edit.text = target.address
	_name_edit.text = target.display_name
	_populate_backend_picker()
	_select_template_for(target.backend)
	_refresh_address_hint()
	popup_centered()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	for backend in _templates:
		_backend_picker.add_item(ConnectUiShared.format_backend_label(backend))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0


func _select_template_for(backend: BackendPeer) -> void:
	if backend == null:
		return
	for i in _templates.size():
		var t := _templates[i]
		if t.get_script() == backend.get_script() and t.get_script() != null:
			_backend_picker.selected = i
			return
		if t.get_class() == backend.get_class() and t.get_script() == null:
			_backend_picker.selected = i
			return


func _selected_template() -> BackendPeer:
	if _templates.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _templates.size():
		return null
	return _templates[idx]


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
	var target: JoinTarget = _editing if _editing else JoinTarget.new()
	target.address = _address_edit.text
	target.backend = template
	target.display_name = (
			_name_edit.text if not _name_edit.text.is_empty()
			else ConnectUiShared.format_address(target)
	)
	hide()
	submitted.emit(target)
