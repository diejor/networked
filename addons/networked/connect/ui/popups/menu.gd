## A pre-configured popup context menu for server row actions.
class_name Menu
extends PopupMenu

const ID_JOIN := 0
const ID_EDIT := 2
const ID_REMOVE := 3


## Pops up the menu at the specified [param screen_position], dynamically
## configuring Edit and Remove options based on [param is_saved].
func show_for_target(is_saved: bool, screen_position: Vector2) -> void:
	if item_count >= 4:
		set_item_disabled(2, not is_saved)
		set_item_disabled(3, not is_saved)

	popup(Rect2i(Vector2i(screen_position), Vector2i.ZERO))
