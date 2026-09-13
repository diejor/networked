## A single detail item shown in the [ConnectBrowser] details container.
##
## Displays a top-level category label and a larger value label underneath
## in a stacked layout.
class_name DetailItem
extends VBoxContainer

@onready var _title_label: Label = %TitleLabel
@onready var _value_label: Label = %ValueLabel


## Updates the displayed [param title] and [param value] texts.
func set_detail(title: String, value: String) -> void:
	if not is_inside_tree():
		await ready
	_title_label.text = title
	_value_label.text = value
