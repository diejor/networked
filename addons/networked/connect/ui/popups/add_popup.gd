## Modal form for adding and editing targets in the [ConnectBrowser].
class_name AddPopup
extends PopupPanel

signal submitted(target: NetwConnectTarget)

var _transports: Array[NetwTransport] = []
var _editing: NetwConnectTarget = null

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
	_backend_picker.item_selected.connect(_on_transport_changed)


## Sets the transports offered in the picker.
func set_transports(transports: Array[NetwTransport]) -> void:
	_transports = transports


## Opens the popup as an empty Add Server form.
func open_add() -> void:
	_editing = null
	_title.text = "Add server"
	_confirm_button.text = "Add"
	_address_edit.text = ""
	_name_edit.text = ""
	_populate_transport_picker()
	_refresh_address_hint()
	popup_centered()


## Opens the popup as an Edit form populated from [param target].
func open_edit(target: NetwConnectTarget) -> void:
	_editing = target
	_title.text = "Edit server"
	_confirm_button.text = "Save"
	_address_edit.text = target.address
	_name_edit.text = target.display_name
	_populate_transport_picker()
	_select_transport_for(target.scheme)
	_refresh_address_hint()
	popup_centered()


func _populate_transport_picker() -> void:
	_backend_picker.clear()
	for transport in _transports:
		_backend_picker.add_item(transport._display_name())
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0


func _select_transport_for(scheme: StringName) -> void:
	for i in _transports.size():
		if _transports[i]._scheme() == scheme:
			_backend_picker.selected = i
			return


func _selected_transport() -> NetwTransport:
	if _transports.is_empty():
		return null
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _transports.size():
		return null
	return _transports[idx]


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
	var target: NetwConnectTarget = _editing if _editing else NetwConnectTarget.new()
	target.scheme = transport._scheme()
	target.address = _address_edit.text
	var typed_name := _name_edit.text.strip_edges()
	target.display_name = typed_name if not typed_name.is_empty() else ConnectBrowser.PLACEHOLDER_SERVER_NAME

	hide()
	submitted.emit(target)
