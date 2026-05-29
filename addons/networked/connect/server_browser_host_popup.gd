## Modal form for hosting a new session, either through a direct
## [BackendPeer] template or through a registered [LobbyProvider].
##
## Configure the choices with [method set_choices] before opening.
## Confirmation fires [signal submitted]; cancellation hides the popup
## without emitting.
@tool
class_name ServerBrowserHostPopup
extends PopupPanel


## Discriminates between the two hosting paths.
enum Kind { DIRECT, PROVIDER }


## Emitted when the user confirms hosting. [param kind] is
## [constant Kind.DIRECT] for backend-template hosts or
## [constant Kind.PROVIDER] for lobby-provider hosts.
## [param choice] holds the [BackendPeer] template (direct) or the
## [StringName] provider id (provider). [param display_name] is the
## name the user typed.
signal submitted(kind: Kind, choice: Variant, display_name: String)


class _Choice:
	var kind: Kind
	var template: BackendPeer
	var provider_id: StringName


var _choices: Array[_Choice] = []


@onready var _picker: OptionButton = %ChoicePicker
@onready var _name_edit: LineEdit = %NameEdit
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)


## Populates the picker with direct backends and provider ids. Call
## before [method open].
func set_choices(
	templates: Array[BackendPeer],
	provider_ids: Array[StringName],
) -> void:
	_choices.clear()
	_picker.clear()
	for backend in templates:
		var c := _Choice.new()
		c.kind = Kind.DIRECT
		c.template = backend
		_choices.append(c)
		_picker.add_item("Direct: %s" % _template_label(backend))
	for id in provider_ids:
		var c := _Choice.new()
		c.kind = Kind.PROVIDER
		c.provider_id = id
		_choices.append(c)
		_picker.add_item("Provider: %s" % String(id).capitalize())


## Resets the form and shows the popup.
func open() -> void:
	_name_edit.text = ""
	if _picker.item_count > 0:
		_picker.selected = 0
	popup_centered()


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
	if _choices.is_empty():
		return
	var idx := maxi(0, _picker.selected)
	if idx >= _choices.size():
		return
	var c := _choices[idx]
	var choice_value: Variant = (
		c.template if c.kind == Kind.DIRECT else c.provider_id
	)
	hide()
	submitted.emit(c.kind, choice_value, _name_edit.text)
