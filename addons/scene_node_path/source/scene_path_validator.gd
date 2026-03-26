class_name ScenePathValidator
extends Object

class ValidationResult extends RefCounted:
	var is_broken: bool = true
	var dynamic_class: String = "Node"
	var warning_msg: String = ""

static func validate(scene_path: String, node_path: String, mod_time: int) -> CacheState:
	var result := CacheState.new()
	result.scene_path = scene_path
	result.node_path = node_path
	result.mod_time = mod_time
	
	if not ResourceLoader.exists(scene_path):
		result.warning_msg = "Scene file does not exist at path: %s" % scene_path
		return result
	
	var packed: PackedScene = load(scene_path)
	if not packed:
		result.warning_msg = "Failed to load PackedScene."
		return result
	
	var temp_instance: Node = packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
	if not temp_instance: 
		return result
	
	var target_node: Node = temp_instance.get_node_or_null(node_path)
	if target_node:
		result.is_broken = false
		result.dynamic_class = target_node.get_class()
	else:
		result.warning_msg = "Node '%s' does not exist." % node_path
	
	temp_instance.free()
	return result
