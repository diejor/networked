class_name Whiteboard
extends Node2D

var stroke: Stroke
var strokes: Node2D


func _init() -> void:
	Netw.configure_entity(self).entity_id = &"whiteboard"
	Netw.configure_spawn(spawn_stroke)


func _ready() -> void:
	strokes = $Strokes


func spawn_stroke(color: Color) -> Node:
	var line := Stroke.new()
	line.default_color = color
	line.width = 2.0
	return line


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed(&"draw"):
		stroke = Netw.spawn(spawn_stroke, Color.BLACK)
		strokes.add_child(stroke)
	elif event.is_action_released(&"draw"):
		stroke = null
	elif stroke != null and event is InputEventMouseMotion:
		stroke.add_point(get_local_mouse_position())


func erase_mine() -> void:
	for line: Stroke in strokes.get_children():
		if line.entity.is_controlled_locally:
			Netw.despawn(line)


func clear() -> void:
	for line: Stroke in strokes.get_children():
		Netw.despawn(line)
