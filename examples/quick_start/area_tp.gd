@tool
class_name AreaTP2D
extends Area2D

signal teleport(body: Node2D)

@export_file("*.tscn") var target_scene: String = "":
	set(value):
		target_scene = value
		update_configuration_warnings()

@export var target_marker: NodePath:
	set(value):
		target_marker = value
		update_configuration_warnings()


func _init() -> void:
	unique_name_in_owner = true
	body_entered.connect(on_body_entered)


func _get_configuration_warnings() -> PackedStringArray:
	var warnings := PackedStringArray()
	if target_scene.is_empty():
		warnings.append("A target_scene must be assigned.")
	if target_marker.is_empty():
		warnings.append("A target_marker must be assigned.")
	return warnings


func on_body_entered(body: Node2D) -> void:
	if not is_inside_tree() or not body.is_inside_tree():
		return
	var tp: TPComponent = body.get_node_or_null("%TPComponent")
	if tp == null or not tp.is_multiplayer_authority():
		return
	if tp.is_settling():
		return

	assert(
		not target_scene.is_empty() and not target_marker.is_empty(),
		"AreaTP2D: `target_scene` and `target_marker` are not both set.",
	)

	teleport.emit(body)
	tp.teleport.call_deferred(target_scene, target_marker)
