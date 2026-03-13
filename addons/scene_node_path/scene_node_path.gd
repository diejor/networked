class_name SceneNodePath
extends Resource

## A resource that stores a reference to a specific [Node] within an external [PackedScene].
##
## Allows for cross-scene node references that survive file movement (via UIDs). 
## Provides methods to safely instantiate the [member scene_path] and retrieve the [Node] at [member node_path].
##
## [br][br]
## You can assign a path via the Inspector, or create one dynamically in code using its string representation.
## [codeblock]
## @export var portal_destination: SceneNodePath
##
## func setup_level() -> void:
##     # Using the exported resource from the Inspector
##     var portal = portal_destination.unwrap_into(self)
##     
##     # Creating a resource dynamically via code
##     var boss_data = SceneNodePath.new("uid://b4x8...::%Boss")
##     var boss_node = boss_data.unwrap_into(self)
## [/codeblock]
##
## [br][b]Type-Filtered Export:[/b]
## Restrict the inspector dialog to only allow selecting specific node classes using [annotation @export_custom].
## [codeblock]
## # Only allow selecting Area3D nodes (or scripts extending Area3D)
## @export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:Area3D")
## var trigger_zone: SceneNodePath
## [/codeblock]
##
## [b]Array Type-Filtered Export:[/b]
## To enforce node types within an Array, use [constant PROPERTY_HINT_ARRAY_TYPE] alongside Godot's internal type IDs. 
## The prefix [code]"24/17:"[/code] stands for [code]TYPE_OBJECT[/code] (24) and [code]PROPERTY_HINT_RESOURCE_TYPE[/code] (17).
## [codeblock]
## # An array of paths that only accepts Sprite2D nodes
## @export_custom(PROPERTY_HINT_ARRAY_TYPE, "24/17:SceneNodePath:Sprite2D")
## var sprite_targets: Array[SceneNodePath]
## [/codeblock]

## The path to the scene file. Stored as a UID when possible to prevent broken references.
@export_file("*.tscn") var scene_path: String

## The internal path to the target node within the referenced scene.
@export var node_path: String

## Constructs a new [SceneNodePath]. Optionally accepts a formatted 
## [String] (e.g., [code]"scene_path::node_path"[/code]).
func _init(formatted_path: String = "") -> void:
	if formatted_path.is_empty():
		return
		
	assert(formatted_path.contains("::"), "SceneNodePath: Invalid string format.")
	var parts := formatted_path.split("::", true, 1)
	
	node_path = parts[1]
	
	var raw_scene: String = parts[0]
	if raw_scene.begins_with("res://") and ResourceLoader.exists(raw_scene):
		var uid: int = ResourceLoader.get_resource_uid(raw_scene)
		scene_path = ResourceUID.id_to_text(uid) if uid != ResourceUID.INVALID_ID else raw_scene
	else:
		scene_path = raw_scene

## Returns [code]true[/code] if both paths are assigned and the target scene file exists.
func is_valid() -> bool:
	return not scene_path.is_empty() and not node_path.is_empty() and ResourceLoader.exists(scene_path)

## Instantiates the scene referenced by [member scene_path], adds the scene root as a child of [param parent], and returns the [Node] at [member node_path].
## [codeblock]
## @export var level_data: SceneNodePath
##
## func spawn_level() -> void:
##     var spawn_point = level_data.unwrap_into(self)
##     player.global_position = spawn_point.global_position
## [/codeblock]
func unwrap_into(parent: Node) -> Node:
	var target_node: Node = _instantiate_and_find(true)
	parent.add_child(target_node.owner if target_node.owner else target_node)
	return target_node

## Identical to [method unwrap_into], but safely returns [code]null[/code] on failure without asserting.
func unwrap_into_or_null(parent: Node) -> Node:
	var target_node: Node = _instantiate_and_find(false)
	if target_node:
		parent.add_child(target_node.owner if target_node.owner else target_node)
	return target_node

## Returns the absolute file path and node path combined (e.g., [code]"res://scene.tscn::Node"[/code]).
func as_path() -> String:
	var real_scene: String = scene_path
	if real_scene.begins_with("uid://"):
		var id: int = ResourceUID.text_to_id(real_scene)
		if ResourceUID.has_id(id):
			real_scene = ResourceUID.get_id_path(id)
	return "%s::%s" % [real_scene, node_path]

## Returns the UID path and node path combined (e.g., [code]"uid://...::Node"[/code]).
func as_uid() -> String:
	var uid_scene: String = scene_path
	if not uid_scene.begins_with("uid://") and ResourceLoader.exists(uid_scene):
		var id: int = ResourceLoader.get_resource_uid(uid_scene)
		if id != ResourceUID.INVALID_ID:
			uid_scene = ResourceUID.id_to_text(id)
	return "%s::%s" % [uid_scene, node_path]

## Overrides the default [method Object._to_string] behavior for cleaner debugging.
func _to_string() -> String:
	if scene_path.is_empty() and node_path.is_empty():
		return "<SceneNodePath: Empty>"
	return "<SceneNodePath: %s>" % as_path()

## Loads a [SceneNodePath] from disk, instantiates its scene, adds it to [param parent], and returns the target [Node].
static func load_and_unwrap_into(tres_path: String, parent: Node) -> Node:
	var res = load(tres_path) as SceneNodePath
	assert(res, "SceneNodePath: Resource at %s is invalid or missing." % tres_path)
	return res.unwrap_into(parent)

# ==========================================
# Private Helpers
# ==========================================

func _instantiate_and_find(strict: bool) -> Node:
	if strict:
		assert(not scene_path.is_empty(), "SceneNodePath: scene_path is empty.")
		assert(not node_path.is_empty(), "SceneNodePath: node_path is empty.")
	elif not is_valid():
		return null
		
	var packed_scene: PackedScene = load(scene_path)
	if strict:
		assert(packed_scene, "SceneNodePath: Failed to load scene at %s" % scene_path)
	elif not packed_scene:
		return null
		
	var scene_instance: Node = packed_scene.instantiate()
	var target_node: Node = scene_instance.get_node_or_null(node_path)
	
	if not target_node:
		scene_instance.free()
		if strict:
			assert(false, "SceneNodePath: Could not find node at %s" % node_path)
		return null
		
	return target_node
