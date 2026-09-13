## Modal form for clients to configure join options.
##
## Renders one field per entry of [method NetwConnectHandle.join_schema] and
## one per key of the picked endpoint's [code]client_settings[/code], seeded
## with whatever that bookmark was saved with.
class_name JoinPopup
extends PopupPanel

signal submitted(
		settings: Dictionary,
		username: StringName,
		join_args: Array,
)

var _connection: NetwConnectHandle
var _authored: Dictionary = { }
var _settings := ConnectFieldList.new()
var _join_args := ConnectFieldList.new()

@onready var _username_edit: LineEdit = %UsernameEdit
@onready var _settings_container: VBoxContainer = %SettingsContainer
@onready var _args_container: VBoxContainer = %ArgsContainer
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)


## Opens the join popup for the endpoint on [param peer_class], rendering one
## field per entry of [param handle]'s [method
## NetwConnectHandle.join_schema] and one per client setting that transport
## names, seeded from [param authored].
func open_join(
		handle: NetwConnectHandle,
		default_username: String,
		peer_class: StringName = &"",
		authored: Dictionary = { },
) -> void:
	_connection = handle
	_authored = authored.duplicate(true)
	_username_edit.text = default_username
	_populate_settings(peer_class)
	_populate_join_args()
	popup_centered()


func _populate_settings(peer_class: StringName) -> void:
	var defaults: Dictionary = { }
	if _connection != null and not peer_class.is_empty():
		var transport := _connection.transport(peer_class)
		defaults = transport.get("client_settings", { })
	var seeded: Dictionary = defaults.duplicate(true)
	for key: Variant in _authored.keys():
		if seeded.has(key):
			seeded[key] = _authored[key]
	_settings.render_settings(_settings_container, seeded)


func _populate_join_args() -> void:
	if _connection == null:
		_join_args.render_schema(_args_container, [])
		return
	_join_args.render_schema(_args_container, _connection.join_schema())


func _on_confirm() -> void:
	var typed := _username_edit.text.strip_edges()
	var username := StringName(typed) if not typed.is_empty() else &"Player"

	hide()
	submitted.emit(
		_settings.as_dictionary(),
		username,
		_join_args.as_array(),
	)
