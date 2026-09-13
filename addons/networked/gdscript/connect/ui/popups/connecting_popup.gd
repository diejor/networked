## Progress overlay displayed during server connection handshakes.
class_name ConnectingPopup
extends PopupPanel

signal cancelled

@onready var _title: Label = %TitleLabel
@onready var _progress: ProgressBar = %ProgressBar
@onready var _cancel_button: Button = %CancelButton


func _ready() -> void:
	visible = false
	popup_window = false
	exclusive = true
	_cancel_button.pressed.connect(_on_cancel)


## Displays the connecting screen and updates details from the endpoint
## [param peer_class] and [param address] name on [param handle].
##
## An empty [param peer_class] is a host bring-up, which has no address to
## render and is otherwise the same wait with the same cancel.
func open_connecting(
		handle: NetwConnectHandle,
		peer_class: StringName,
		address: String,
) -> void:
	_cancel_button.text = "Cancel"
	_progress.visible = false
	if handle == null or peer_class.is_empty():
		_title.text = "Starting server..."
		popup_centered()
		return
	var backend_name := ConnectBrowser.format_peer_class_label(peer_class)
	var display_addr := address.strip_edges()
	if display_addr.is_empty():
		_title.text = "Connecting to %s server..." % backend_name
	else:
		_title.text = (
				"Connecting to %s server at %s..."
				% [backend_name, display_addr]
		)
	popup_centered()


## Updates the displayed progress [param message] and determinate [param ratio].
func update_progress(message: String, ratio: float) -> void:
	if not message.is_empty():
		_title.text = message
	_progress.visible = ratio >= 0.0
	if ratio >= 0.0:
		_progress.value = ratio


## Displays the failure screen with [param message].
func show_failed(message: String, detail: String = "") -> void:
	_title.text = message
	if not detail.is_empty():
		_title.text += "\n%s" % detail
	_progress.visible = false
	_cancel_button.text = "Close"
	popup_centered()


func _on_cancel() -> void:
	hide()
	cancelled.emit()
