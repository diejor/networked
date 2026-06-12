## Popup confirming if the client should host instead after connection failure.
class_name HostFallbackPopup
extends PopupPanel

signal submitted(target: JoinTarget)

var _pending_target: JoinTarget = null

@onready var _title: Label = %TitleLabel
@onready var _confirm_button: Button = %ConfirmButton
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_confirm_button.pressed.connect(_on_confirm)
	_cancel_button.pressed.connect(hide)


## Opens the host fallback popup for the specified [param target].
func open_host_fallback(target: JoinTarget) -> void:
	_pending_target = target
	var backend_name := ConnectUiShared.format_backend_label(target.backend)
	var display_addr := target.address.strip_edges()
	if display_addr.is_empty():
		_title.text = (
				"Could not connect to the %s server.\n"
				+ "Would you like to host a server instead?"
		) % backend_name
	else:
		_title.text = (
				"Could not connect to the %s server at %s.\n"
				+ "Would you like to host a server instead?"
		) % [backend_name, display_addr]
	popup_centered()


func _on_confirm() -> void:
	hide()
	submitted.emit(_pending_target)
