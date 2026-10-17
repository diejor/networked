@tool
extends EditorExportPlugin

func _get_name() -> String:
	return "NetworkedSteam"


func _begin_customize_scenes(
		_platform: EditorExportPlatform,
		features: PackedStringArray,
) -> bool:
	var manifest := ConfigFile.new()
	manifest.load("res://addons/godotsteam/godotsteam.gdextension")
	for key: String in manifest.get_section_keys("libraries"):
		var matches := true
		for feature: String in key.split("."):
			matches = matches and features.has(feature)
		if matches:
			return false
	return true


func _get_customization_configuration_hash() -> int:
	return 1


func _customize_scene(scene: Node, _path: String) -> Node:
	var nodes: Array[Node] = [scene]
	nodes.append_array(scene.find_children("*", "", true, false))
	for node: Node in nodes:
		if not is_instance_valid(node):
			continue
		var script: Script = node.get_script()
		if script == null or not script.resource_path.ends_with(
			"/steam_lobby_directory.gd",
		):
			continue
		if node == scene:
			node.set_script(null)
			continue
		node.get_parent().remove_child(node)
		node.free()
	return scene
