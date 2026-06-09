## Progress overlay displayed during server connection handshakes.
class_name ConnectingPopup
extends PopupPanel

signal cancelled

@onready var _title: Label = %TitleLabel
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	_cancel_button.pressed.connect(_on_cancel)


## Displays the connecting screen and updates details from [param target].
func open_connecting(target: JoinTarget) -> void:
	_cancel_button.text = "Cancel"
	var backend_name := ConnectUiShared.format_backend_label(target.backend)
	var display_addr := target.address.strip_edges()
	if display_addr.is_empty():
		_title.text = "Connecting to %s server..." % backend_name
	else:
		_title.text = (
				"Connecting to %s server at %s..."
				% [backend_name, display_addr]
		)
	popup_centered()


## Displays the failure screen with [param message].
func show_failed(message: String) -> void:
	_title.text = message
	_cancel_button.text = "Close"
	popup_centered()


func _on_cancel() -> void:
	hide()
	cancelled.emit()
