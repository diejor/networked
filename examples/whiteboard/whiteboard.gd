class_name Whiteboard
extends Node2D

var ink := PackedVector2Array():
	set(value):
		ink = value
		queue_redraw()
var breaks := PackedInt32Array():
	set(value):
		breaks = value
		queue_redraw()
var drawing := false

var entity: NetwEntity


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.entity_id = &"whiteboard"
	entity.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"ink").broadcast()
	Netw.configure_property(self, &"breaks").broadcast()


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed(&"draw"):
		entity.request_control(NetwEntity.HOLD_EXCLUSIVE).catch_error(refused)
		drawing = entity.is_controlled_locally
		if drawing:
			breaks.append(ink.size())
	elif event.is_action_released(&"draw") and drawing:
		drawing = false
		entity.release_control()
	elif drawing and event is InputEventMouseMotion:
		ink.append(get_local_mouse_position())
		queue_redraw()


func refused(_code: Error, _detail: String) -> void:
	drawing = false


func _draw() -> void:
	var starts := Array(breaks)
	starts.append(ink.size())
	for at in starts.size() - 1:
		var stroke := ink.slice(starts[at], starts[at + 1])
		if stroke.size() > 1:
			draw_polyline(stroke, Color.BLACK, 2.0)
