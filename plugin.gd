@tool
extends EditorPlugin

const SceneNodePathPlugin = preload("uid://dtj5ucl1iy3ug")
var scene_node_path_plugin: EditorPlugin

func _enter_tree() -> void:
	scene_node_path_plugin = SceneNodePathPlugin.new()
	add_child(scene_node_path_plugin)

func _exit_tree() -> void:
	if is_instance_valid(scene_node_path_plugin):
		scene_node_path_plugin.queue_free()
