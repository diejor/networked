## A pre-configured popup context menu for server row actions.
class_name Menu
extends PopupMenu

const ID_JOIN := 0
const ID_EDIT := 2
const ID_REMOVE := 3


## Shows the menu at [param screen_position]. Edit and Remove are enabled
## only when [param is_editable].
func show_for_target(is_editable: bool, screen_position: Vector2) -> void:
	if item_count >= 4:
		set_item_disabled(2, not is_editable)
		set_item_disabled(3, not is_editable)

	popup(Rect2i(Vector2i(screen_position), Vector2i.ZERO))
