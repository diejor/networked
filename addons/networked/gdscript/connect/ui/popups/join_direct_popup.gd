## A form to join a server by its address.
class_name JoinDirectPopup
extends PopupPanel

signal submitted(
		transport: StringName,
		address: String,
		settings: Dictionary,
		username: String,
		join_args: Array,
)

var transports: Array[Dictionary] = []
var settings := ConnectFieldList.new()
var join_args := ConnectFieldList.new()

@onready var transport_picker: OptionButton = %BackendPicker
@onready var address_edit: LineEdit = %AddressEdit
@onready var username_edit: LineEdit = %UsernameEdit
@onready var settings_container: VBoxContainer = %SettingsContainer
@onready var args_container: VBoxContainer = %ArgsContainer
@onready var confirm_button: Button = %ConfirmButton
@onready var cancel_button: Button = %CancelButton


func _ready() -> void:
	hide()
	transport_picker.item_selected.connect(on_transport_selected)
	confirm_button.pressed.connect(on_confirm_pressed)
	cancel_button.pressed.connect(hide)


func open(
		connection: NetwConnection,
		username: String,
		peer_class := &"",
		address := "",
) -> void:
	transports.clear()
	transport_picker.clear()
	for transport in connection.transports():
		if transport.capabilities & NetwConnection.TRANSPORT_AVAILABLE:
			transports.append(transport)
			transport_picker.add_item(transport.display_name)
	var peer_classes := transports.map(func(entry): return entry.peer_class)
	transport_picker.selected = maxi(0, peer_classes.find(peer_class))
	address_edit.text = address
	username_edit.text = username
	join_args.render_schema(args_container, connection.join_schema())
	on_transport_selected(transport_picker.selected)
	popup_centered()


func on_transport_selected(index: int) -> void:
	var transport := transports[index]
	address_edit.placeholder_text = transport.address_placeholder
	address_edit.tooltip_text = transport.address_help
	settings.render_settings(settings_container, transport.client_settings)


func on_confirm_pressed() -> void:
	hide()
	submitted.emit(
		transports[transport_picker.selected].peer_class,
		address_edit.text.strip_edges(),
		settings.as_dictionary(),
		(
				username_edit.text if not username_edit.text.is_empty()
				else username_edit.placeholder_text
		),
		join_args.as_array(),
	)
