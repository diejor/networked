## Tiles several [ParticipantWindow] nodes inside the enclosing viewport.
##
## Mouse and keyboard input are routed by native window focus. Joypad input goes
## to the slot assigned to the event's device id with [method assign_device].
class_name ParticipantViewport
extends Node

## When [code]true[/code], child [Window] nodes are embedded in the root window.
@export var embed_subwindows := true

var _slots: Array[ParticipantWindow] = []
var _device_slots: Dictionary[int, ParticipantWindow] = { }


func _ready() -> void:
	set_process_input(true)
	var viewport := get_viewport()
	if viewport:
		viewport.gui_embed_subwindows = embed_subwindows
		if not viewport.size_changed.is_connected(_relayout):
			viewport.size_changed.connect(_relayout)
	_relayout()


func _exit_tree() -> void:
	var viewport := get_viewport()
	if viewport and viewport.size_changed.is_connected(_relayout):
		viewport.size_changed.disconnect(_relayout)
	for slot: ParticipantWindow in _slots.duplicate():
		remove_slot(slot)


## Adds [param slot] to the tiled window set.
func add_slot(slot: ParticipantWindow) -> ParticipantWindow:
	if _slots.has(slot):
		slot.visible = true
		return slot

	_slots.append(slot)
	slot.visible = true
	_relayout()
	return slot


## Removes [param slot] from the tiled window set.
func remove_slot(slot: ParticipantWindow) -> void:
	if not _slots.has(slot):
		return

	for device_id: int in _device_slots.keys().duplicate():
		if _device_slots[device_id] == slot:
			_device_slots.erase(device_id)

	_slots.erase(slot)
	if is_instance_valid(slot):
		slot.visible = false
	_relayout()


## Returns whether [param slot] is registered for tiling.
func has_slot(slot: ParticipantWindow) -> bool:
	return _slots.has(slot)


## Routes joypad events from [param device_id] to [param slot].
func assign_device(device_id: int, slot: ParticipantWindow) -> void:
	assert(_slots.has(slot), "ParticipantViewport: unknown slot.")
	_device_slots[device_id] = slot


func _input(event: InputEvent) -> void:
	if not (event is InputEventJoypadButton or event is InputEventJoypadMotion):
		return

	var slot := _device_slots.get(event.device) as ParticipantWindow
	if not is_instance_valid(slot):
		return

	slot.send_input(event)
	get_viewport().set_input_as_handled()


func _relayout() -> void:
	var count := _slots.size()
	if count == 0:
		return

	var viewport := get_viewport()
	if not viewport:
		return

	var rect := viewport.get_visible_rect()
	var columns := int(ceil(sqrt(float(count))))
	var rows := int(ceil(float(count) / float(columns)))
	var cell := Vector2(rect.size.x / float(columns), rect.size.y / float(rows))

	for index in count:
		var slot := _slots[index]
		if not is_instance_valid(slot):
			continue
		var col := index % columns
		var row := index / columns
		var tile_position := Vector2i(
			int(rect.position.x + float(col) * cell.x),
			int(rect.position.y + float(row) * cell.y),
		)
		var tile_size := Vector2i(int(cell.x), int(cell.y))
		slot.set_tiled_rect(Rect2i(tile_position, tile_size))
