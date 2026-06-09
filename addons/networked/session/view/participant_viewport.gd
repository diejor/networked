## Displays several [ParticipantSlot] views inside one [Control].
##
## Mouse input is routed by each [ParticipantView] tile. Keyboard input goes
## to the focused tile. Joypad input goes to the tile assigned to the event's
## device id with [method assign_device].
class_name ParticipantViewport
extends Control

var _views_by_slot: Dictionary[ParticipantSlot, ParticipantView] = { }
var _slots_by_view: Dictionary[ParticipantView, ParticipantSlot] = { }
var _host_views_by_slot: Dictionary[ParticipantSlot, HostSceneView] = { }
var _source_changed_by_slot: Dictionary[ParticipantSlot, Callable] = { }
var _device_views: Dictionary[int, ParticipantView] = { }
var _focused_view: ParticipantView = null


func _ready() -> void:
	resized.connect(_relayout)
	set_process_input(true)
	_sync_root_sized_rect()
	var root := get_tree().root
	if not root.size_changed.is_connected(_sync_root_sized_rect):
		root.size_changed.connect(_sync_root_sized_rect)


func _exit_tree() -> void:
	var root := get_tree().root
	if root and root.size_changed.is_connected(_sync_root_sized_rect):
		root.size_changed.disconnect(_sync_root_sized_rect)
	for slot: ParticipantSlot in _views_by_slot.keys().duplicate():
		remove_slot(slot)


## Adds [param slot] to the displayed viewport.
func add_slot(slot: ParticipantSlot) -> ParticipantView:
	if _views_by_slot.has(slot):
		return _views_by_slot[slot]

	_suppress_host_view(slot)

	var view := ParticipantView.new()
	view.name = "%sView" % slot.name
	view.focus_mode = Control.FOCUS_CLICK
	view.mouse_filter = Control.MOUSE_FILTER_STOP
	view.set_target(slot.display_source())
	view.focus_entered.connect(_set_focused_from_signal.bind(view))
	add_child(view)

	_views_by_slot[slot] = view
	_slots_by_view[view] = slot
	var source_changed := _on_slot_display_source_changed.bind(slot)
	_source_changed_by_slot[slot] = source_changed
	if not slot.display_source_changed.is_connected(source_changed):
		slot.display_source_changed.connect(source_changed)
	if not is_instance_valid(_focused_view):
		set_focus(view)
	_relayout()
	return view


## Removes [param slot] from the displayed viewport.
func remove_slot(slot: ParticipantSlot) -> void:
	if not _views_by_slot.has(slot):
		return
	var view := _views_by_slot[slot] as ParticipantView
	var callable: Callable = _source_changed_by_slot.get(slot, Callable())
	if slot.display_source_changed.is_connected(callable):
		slot.display_source_changed.disconnect(callable)

	for device_id: int in _device_views.keys().duplicate():
		if _device_views[device_id] == view:
			_device_views.erase(device_id)

	_slots_by_view.erase(view)
	_views_by_slot.erase(slot)
	_source_changed_by_slot.erase(slot)
	if _focused_view == view:
		_focused_view = null
	view.clear_target()
	view.queue_free()
	_restore_host_view(slot)

	if not is_instance_valid(_focused_view) and not _views_by_slot.is_empty():
		set_focus(_views_by_slot.values()[0] as ParticipantView)
	_relayout()


## Returns the [ParticipantView] displaying [param slot], or [code]null[/code].
func view_for_slot(slot: ParticipantSlot) -> ParticipantView:
	return _views_by_slot.get(slot) as ParticipantView


## Routes joypad events from [param device_id] to [param view].
func assign_device(device_id: int, view: ParticipantView) -> void:
	assert(_slots_by_view.has(view), "ParticipantViewport: unknown view.")
	_device_views[device_id] = view


## Routes keyboard events to [param view].
func set_focus(view: ParticipantView) -> void:
	assert(_slots_by_view.has(view), "ParticipantViewport: unknown view.")
	_focused_view = view
	if is_inside_tree() and view.is_inside_tree():
		view.grab_focus()


func _input(event: InputEvent) -> void:
	if event is InputEventMouse:
		return

	var view := _view_for_event(event)
	if not is_instance_valid(view):
		return

	view.forward_input(event)
	get_viewport().set_input_as_handled()


func _relayout() -> void:
	var views: Array = _views_by_slot.values()
	var count := views.size()
	if count == 0:
		return
	var columns := int(ceil(sqrt(float(count))))
	var rows := int(ceil(float(count) / float(columns)))
	var cell := Vector2(size.x / float(columns), size.y / float(rows))

	for index in count:
		var view := views[index] as ParticipantView
		var col := index % columns
		var row := index / columns
		view.anchor_left = 0.0
		view.anchor_top = 0.0
		view.anchor_right = 0.0
		view.anchor_bottom = 0.0
		view.position = Vector2(float(col) * cell.x, float(row) * cell.y)
		view.size = cell


func _view_for_event(event: InputEvent) -> ParticipantView:
	if event is InputEventJoypadButton or event is InputEventJoypadMotion:
		return _device_views.get(event.device) as ParticipantView
	return _focused_view


func _on_slot_display_source_changed(
		viewport: SubViewport,
		slot: ParticipantSlot,
) -> void:
	var view := _views_by_slot.get(slot) as ParticipantView
	if view:
		view.set_target(viewport)


func _set_focused_from_signal(view: ParticipantView) -> void:
	_focused_view = view


func _suppress_host_view(slot: ParticipantSlot) -> void:
	if not slot.tree or not slot.tree.is_host:
		return
	var host_view := slot.tree.get_service(HostSceneView) as HostSceneView
	if not host_view:
		host_view = slot.tree.find_service_node(HostSceneView) as HostSceneView
	if not host_view:
		return
	host_view.set_suppressed(true)
	_host_views_by_slot[slot] = host_view


func _restore_host_view(slot: ParticipantSlot) -> void:
	var host_view := _host_views_by_slot.get(slot) as HostSceneView
	_host_views_by_slot.erase(slot)
	if is_instance_valid(host_view):
		host_view.set_suppressed(false)


func _sync_root_sized_rect() -> void:
	if get_parent() is Control:
		set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		return
	var rect := get_tree().root.get_visible_rect()
	anchor_left = 0.0
	anchor_top = 0.0
	anchor_right = 0.0
	anchor_bottom = 0.0
	position = Vector2.ZERO
	size = rect.size
