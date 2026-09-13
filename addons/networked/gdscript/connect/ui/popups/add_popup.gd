## Modal form for adding and editing endpoints in the [ConnectBrowser].
##
## Renders one field per key of the [code]client_settings[/code] entry of
## [method NetwConnectHandle.transport] for the picked backend, so a bookmark
## remembers the tracker or port it was reached on.
class_name AddPopup
extends PopupPanel

signal submitted(
		peer_class: StringName,
		address: String,
		display_name: String,
		settings: Dictionary,
		editing_peer_class: StringName,
		editing_address: String,
)

var _connection: NetwConnectHandle
var _transports: Array[Dictionary] = []
var _editing_peer_class: StringName = &""
var _editing_address: String = ""
var _authored: Dictionary = { }
var _settings := ConnectFieldList.new()

@onready var _title: Label = %TitleLabel
@onready var _backend_picker: OptionButton = %BackendPicker
@onready var _address_edit: LineEdit = %AddressEdit
@onready var _name_edit: LineEdit = %NameEdit
@onready var _settings_container: VBoxContainer = %SettingsContainer
@onready var _settings_scroll: ScrollContainer = %SettingsScroll
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)
	_backend_picker.item_selected.connect(_on_backend_changed)


## Opens the popup as an empty Add Server form, offering the backends
## [param handle] reports.
func open_add(handle: NetwConnectHandle) -> void:
	_connection = handle
	_editing_peer_class = &""
	_editing_address = ""
	_authored = { }
	_title.text = "Add server"
	_confirm_button.text = "Add"
	_address_edit.text = ""
	_name_edit.text = ""
	_populate_backend_picker()
	popup_centered()


## Opens the popup as an Edit form populated from the endpoint [param
## peer_class] and [param address] name on [param handle], with the settings
## [param authored] holding whatever that bookmark was last saved with.
func open_edit(
		handle: NetwConnectHandle,
		peer_class: StringName,
		address: String,
		authored: Dictionary = { },
) -> void:
	_connection = handle
	_editing_peer_class = peer_class
	_editing_address = address
	_authored = authored.duplicate(true)
	var endpoint := handle.endpoint(peer_class, address)
	_title.text = "Edit server"
	_confirm_button.text = "Save"
	_address_edit.text = String(endpoint.get("address", address))
	_name_edit.text = String(endpoint.get("display_name", ""))
	_populate_backend_picker()
	_select_peer_class(peer_class)
	_refresh_address_hint()
	_populate_settings()
	popup_centered()


func _populate_backend_picker() -> void:
	_backend_picker.clear()
	_transports.clear()
	if _connection != null:
		for transport: Dictionary in _connection.transports():
			var caps := int(transport.get("capabilities", 0))
			if caps & NetwMultiplayer.TRANSPORT_AVAILABLE == 0:
				continue
			_transports.append(transport)
			_backend_picker.add_item(String(transport.get("display_name", "")))
	if _backend_picker.item_count > 0:
		_backend_picker.selected = 0
	_refresh_address_hint()
	_populate_settings()


func _select_peer_class(peer_class: StringName) -> void:
	for i in _transports.size():
		if _transports[i].get("peer_class") == peer_class:
			_backend_picker.selected = i
			return


func _selected_transport() -> Dictionary:
	if _transports.is_empty():
		return { }
	var idx := maxi(0, _backend_picker.selected)
	if idx >= _transports.size():
		return { }
	return _transports[idx]


func _on_backend_changed(_index: int) -> void:
	_refresh_address_hint()
	_populate_settings()


# The defaults name the fields and carry their types, and whatever this
# bookmark was saved with overwrites the value of the ones it names.
func _populate_settings() -> void:
	var transport := _selected_transport()
	var defaults: Dictionary = transport.get("client_settings", { })
	var seeded: Dictionary = defaults.duplicate(true)
	for key: Variant in _authored.keys():
		if seeded.has(key):
			seeded[key] = _authored[key]
	_settings.render_settings(_settings_container, seeded)
	_settings_scroll.visible = not _settings.is_empty()


func _refresh_address_hint() -> void:
	var transport := _selected_transport()
	if _connection == null or transport.is_empty():
		_address_edit.placeholder_text = ""
		_address_edit.tooltip_text = ""
		return
	_address_edit.placeholder_text = String(
		transport.get("address_placeholder", ""),
	)
	_address_edit.tooltip_text = String(transport.get("address_help", ""))


func _on_confirm() -> void:
	var transport := _selected_transport()
	if transport.is_empty():
		return
	var peer_class: StringName = transport.get("peer_class")
	var address := _address_edit.text
	var typed_name := _name_edit.text.strip_edges()
	var display_name := (
			typed_name if not typed_name.is_empty()
			else ConnectBrowser.PLACEHOLDER_SERVER_NAME
	)

	hide()
	submitted.emit(
		peer_class,
		address,
		display_name,
		_settings.as_dictionary(),
		_editing_peer_class,
		_editing_address,
	)
