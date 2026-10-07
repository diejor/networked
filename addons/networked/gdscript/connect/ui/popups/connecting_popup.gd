## The progress of a connection attempt, with a button to cancel it.
class_name ConnectingPopup
extends PopupPanel

signal cancelled

@onready var title_label: Label = %TitleLabel
@onready var progress_bar: ProgressBar = %ProgressBar
@onready var cancel_button: Button = %CancelButton


func _ready() -> void:
	hide()
	cancel_button.pressed.connect(hide)
	cancel_button.pressed.connect(cancelled.emit)


func open() -> void:
	title_label.text = "Connecting..."
	progress_bar.hide()
	cancel_button.text = "Cancel"
	popup_centered()


func progressed(message: String, ratio: float) -> void:
	if not message.is_empty():
		title_label.text = message
	progress_bar.visible = ratio >= 0.0
	progress_bar.value = ratio


func failed(reason: String) -> void:
	title_label.text = reason
	progress_bar.hide()
	cancel_button.text = "Close"
	popup_centered()
