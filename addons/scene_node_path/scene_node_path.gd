@tool
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
##     # 1. Instantiates and adds the scene root to 'self', returning the target.
##     var portal = portal_destination.unwrap_into(self)
##
##     # To safely delete the ENTIRE spawned scene later:
##     SceneNodePath.get_scene_root(portal).queue_free()
##
##     # 2. Pulls the node out and deletes the rest of the scene automatically.
##     var isolated_boss = SceneNodePath.new("uid://b4x8...::%Boss").extract()
##     add_child(isolated_boss)
## [/codeblock]
##
## [b]Type-Filtered Export:[/b]
## Restrict the inspector dialog to only allow selecting specific node classes using [annotation @export_custom].
## [codeblock]
## # Only allow selecting Area3D nodes (or scripts extending Area3D)
## @export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:Area3D")
## var trigger_zone: SceneNodePath
## [/codeblock]
##
## [br][b]Array Type-Filtered Export:[/b]
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

## Internal variable updated by the Inspector plugin to store validation errors.
## Useful for forwarding warnings to a Node's _get_configuration_warnings().
var _editor_property_warnings: String = ""

## Safely retrieves the absolute root of a scene spawned via [method unwrap_into].
## Use this to safely [method Node.queue_free] the entire scene later.
static func get_scene_root(spawned_node: Node) -> Node:
	if not spawned_node: return null
	return spawned_node.get_meta("snp_root", null)

static func _get_orphan_root(node: Node) -> Node:
	if not node: return null
	var current := node
	while current.get_parent() != null:
		current = current.get_parent()
	return current

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


## Returns [code]true[/code] if the [member scene_path] and [member node_path] are not empty
## and the scene file exists on disk.
## [br][br]
## [b]Note:[/b] This is a surface-level check. It does [b]not[/b] verify if the [member node_path]
## actually exists inside the scene file. Use [method SceneNodePath.StateInspector.is_valid] for a more robust validation.
func is_valid() -> bool:
	if scene_path.is_empty() or node_path.is_empty():
		return false
	var real_path := _safe_resolve_path(scene_path)
	return not real_path.is_empty() and ResourceLoader.exists(real_path)


## Instantiates the entire scene referenced by [member scene_path], adds its 
## root as a child of [param parent], 
## and returns the specific target [Node] at [member node_path].
##
## [br][br][b]Important Memory Management Note:[/b]
## Because the entire scene is instantiated, calling [method Node.queue_free] 
## on the returned node will [i]only[/i] delete that specific child, leaving the 
## rest of the instantiated scene in the tree. 
## To safely delete the entire spawned scene, you must free its root node. 
## You can access the root using the [method get_scene_root] static method on 
## the returned node.
func unwrap_into(parent: Node) -> Node:
	var target_node := _instantiate_and_get()
	var root := _get_orphan_root(target_node)
	
	target_node.set_meta("snp_root", root)
	parent.add_child(root)
	
	return target_node


## Identical to [method unwrap_into], but safely returns [code]null[/code] on 
## failure without asserting.
func unwrap_into_or_null(parent: Node) -> Node:
	var target_node := _instantiate_and_get_or_null()
	if target_node:
		var root := _get_orphan_root(target_node)
		target_node.set_meta("snp_root", root)
		parent.add_child(root)
	return target_node


## Instantiates the entire scene referenced by [member scene_path], isolates the target [Node] 
## at [member node_path], and [method Node.queue_free]s the rest of the scene.
## [br][br]
## [b]Warning:[/b] The extracted node is surgically removed from its scene tree via [method Node.remove_child]. 
## It loses its original siblings and parent context.
func extract() -> Node:
	return _perform_extraction(_instantiate_and_get())


## Identical to [method extract], but safely returns [code]null[/code] on failure.
func extract_or_null() -> Node:
	var target := _instantiate_and_get_or_null()
	return _perform_extraction(target) if target else null


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


## Returns the file name of the scene referenced by [member scene_path], excluding the extension.
## [br][br]
## For example, if [member scene_path] is [code]"res://maps/dungeon_01.tscn"[/code], this returns [code]"dungeon_01"[/code].
func get_scene_name() -> String:
	if scene_path.is_empty():
		return ""

	var real_path: String = scene_path
	if real_path.begins_with("uid://"):
		var id: int = ResourceUID.text_to_id(real_path)
		if ResourceUID.has_id(id):
			real_path = ResourceUID.get_id_path(id)

	return real_path.get_file().get_basename()


## Returns the name of the node (or an ancestor) referenced by [member node_path].
## [br][br]
## The [param parent_offset] determines which segment of the [NodePath] to return:
## [br]- [code]0[/code]: The target node's name.
## [br]- [code]1[/code]: The name of the target node's parent.
## [br]- [code]2[/code]: The name of the target node's grandparent, and so on.
func get_node_name(parent_offset: int = 0) -> String:
	if node_path.is_empty() or parent_offset < 0:
		return ""

	var path := NodePath(node_path)
	var name_count := path.get_name_count()

	if name_count == 0 or parent_offset >= name_count:
		return ""

	# Invert the index
	var target_idx: int = (name_count - 1) - parent_offset

	var target_name := String(path.get_name(target_idx))

	# Clean up the name if it's a Scene Unique Node
	if target_name.begins_with("%"):
		target_name = target_name.trim_prefix("%")

	return target_name


func _to_string() -> String:
	if scene_path.is_empty() and node_path.is_empty():
		return "<SceneNodePath: Empty>"
	return "<SceneNodePath: %s>" % as_path()


## Loads a [SceneNodePath] from disk, instantiates its scene, and returns the target [Node].
static func load_instantiate_and_get(tres_path: String) -> Node:
	var res = load(tres_path) as SceneNodePath
	assert(res, "SceneNodePath: Resource at %s is invalid or missing." % tres_path)
	return res.instantiate_and_get()

func _instantiate_and_get() -> Node:
	assert(not scene_path.is_empty(), "SceneNodePath: scene_path is empty.")
	assert(not node_path.is_empty(), "SceneNodePath: node_path is empty.")

	var real_path := _safe_resolve_path(scene_path)
	assert(not real_path.is_empty(), "SceneNodePath: Invalid UID or scene path.")

	var packed_scene: PackedScene = load(real_path)
	assert(packed_scene, "SceneNodePath: Failed to load scene at %s" % real_path)

	var scene_instance: Node = packed_scene.instantiate()
	var target_path := NodePath(node_path)
	var target_node: Node
	
	if target_path == NodePath(".") or node_path == String(scene_instance.name):
		target_node = scene_instance
	else:
		target_node = scene_instance.get_node_or_null(target_path)

	if not target_node:
		scene_instance.free()
		assert(false, "SceneNodePath: Could not find node at %s" % node_path)
		return null

	return target_node


func _instantiate_and_get_or_null() -> Node:
	if not is_valid():
		return null

	var real_path := _safe_resolve_path(scene_path)
	if real_path.is_empty():
		return null

	var packed_scene: PackedScene = load(real_path)
	if not packed_scene:
		return null

	var scene_instance: Node = packed_scene.instantiate()
	var target_path := NodePath(node_path)
	var target_node: Node
	
	if target_path == NodePath(".") or node_path == String(scene_instance.name):
		target_node = scene_instance
	else:
		target_node = scene_instance.get_node_or_null(target_path)

	if not target_node:
		scene_instance.free()
		return null

	return target_node


func _perform_extraction(target: Node) -> Node:
	if not target: return null
	
	var root := _get_orphan_root(target)
	
	if root != target:
		target.get_parent().remove_child(target)
		root.queue_free()
		_clear_ownership(target)
		
	return target


func _clear_ownership(node: Node) -> void:
	node.owner = null
	for child in node.get_children(true):
		_clear_ownership(child)


static func _safe_resolve_path(path: String) -> String:
	if path.is_empty(): 
		return ""
	if path.begins_with("uid://"):
		var id := ResourceUID.text_to_id(path)
		if ResourceUID.has_id(id):
			return ResourceUID.get_id_path(id)
		return "" # Silently fail if the UID is missing
	return path


## Returns a [SceneNodePath.StateInspector] object, allowing you to read the target node's data 
## directly from the [SceneState] without instantiating the scene into memory.
## [br][br]
## This performs a recursive deep-search. It can perfectly read data from nodes buried 
## inside instanced sub-scenes, accurately resolving property overrides.
## [codeblock]
## var inspector = portal_path.peek()
## if inspector.is_valid():
##     print("Target is a: ", inspector.get_node_type())
## [/codeblock]
func peek() -> StateInspector:
	var root_state := _cache.get_valid_state(scene_path)
	var result := _cache.resolve_deep_node(root_state, node_path)
	
	# result[0] is the SceneState where it was finally found, result[1] is the index
	return StateInspector.new(result[0], result[1])

## A transient data object that provides read-only access to a specific node's [SceneState].
##
## [b]Note:[/b] This object is intended to be created via [method SceneNodePath.peek] and 
## should not be instantiated directly.
## [codeblock]
## # 'Hitbox' is inside 'player.tscn', which is instanced inside 'level.tscn'.
## var path = SceneNodePath.new("res://level.tscn::Player/Hitbox")
##
## var inspector = path.peek()
## if inspector.is_valid():
##     print("Found deep node type: ", inspector.get_node_type())
## [/codeblock]
class StateInspector:
	var _state: SceneState
	var _idx: int

	func _init(state: SceneState, idx: int) -> void:
		_state = state
		_idx = idx

	## Returns [code]true[/code] if the target node was found anywhere within the scene file 
	## or its nested sub-scenes.
	func is_valid() -> bool:
		return _state != null and _idx != -1

	## Returns the class type of the target node (e.g., [code]&"Area3D"[/code]).
	func get_node_type() -> StringName:
		return _state.get_node_type(_idx) if is_valid() else &""

	## Returns a [Dictionary] of all exported or overridden property values on the target node.
	## [codeblock]
	## var props = path.peek().get_properties()
	## if props.has("monitoring"):
	##     print("Area3D monitoring: ", props["monitoring"])
	## [/codeblock]
	func get_properties() -> Dictionary:
		var props := {}
		if is_valid():
			for p in _state.get_node_property_count(_idx):
				props[_state.get_node_property_name(_idx, p)] = _state.get_node_property_value(_idx, p)
		return props

	## Returns a specific property value from the scene file, or [param default] if not found.
	## [codeblock]
	## var speed = path.peek().get_property("speed", 10.0)
	## [/codeblock]
	func get_property(prop_name: StringName, default: Variant = null) -> Variant:
		if is_valid():
			for p in _state.get_node_property_count(_idx):
				if _state.get_node_property_name(_idx, p) == prop_name:
					return _state.get_node_property_value(_idx, p)
		return default

	## Returns a [PackedStringArray] of the groups assigned to the node within the scene file.
	## [codeblock]
	## if "enemies" in path.peek().get_groups():
	##     print("This path points to an enemy!")
	## [/codeblock]
	func get_groups() -> PackedStringArray:
		return _state.get_node_groups(_idx) if is_valid() else PackedStringArray()

	## Returns the [PackedScene] for the node if it is a scene instance, or [code]null[/code] if not.
	func get_node_instance() -> PackedScene:
		return _state.get_node_instance(_idx) if is_valid() else null

	## Returns [code]true[/code] if the target node is an [InstancePlaceholder].
	func is_instance_placeholder() -> bool:
		return _state.is_node_instance_placeholder(_idx) if is_valid() else false

	## Returns the path to the represented scene file if the target node is an [InstancePlaceholder].
	func get_instance_placeholder() -> String:
		return _state.get_node_instance_placeholder(_idx) if is_valid() else ""

	## Returns the path to the owner of the target node, relative to the root node of the scene file.
	## [br][br]
	## [b]Note:[/b] For most nodes, this will be [code].[/code] as they are owned by the scene root.
	func get_owner_path() -> NodePath:
		return _state.get_node_owner_path(_idx) if is_valid() else NodePath()

	## Returns the node's index, which is its position relative to its siblings.
	## [br][br]
	## Possible return values:
	## [br]- [code]0[/code]: The node is the first child of its parent (or the root of the scene).
	## [br]- [code]1[/code]: The node is the second child, and so on.
	## [br]- [code]-1[/code]: The [SceneNodePath.StateInspector] is invalid or the node path could not be resolved.
	## [br][br]
	## [b]Note:[/b] This is particularly useful for verifying the structure of a scene 
	## without relying on the custom unwrap logic.
	## [codeblock]
	## var path = SceneNodePath.new("res://player.tscn::Hitbox")
	## var inspector = path.peek()
	##
	## if inspector.is_valid():
	##     var sibling_idx = inspector.get_node_index()
	##     
	##     # Verify the index against a freshly loaded instance
	##     var scene_root = load(path.scene_path).instantiate()
	##     var live_node = scene_root.get_child(sibling_idx)
	##     
	##     print("Verified: %s is child number %d" % [live_node.name, sibling_idx])
	##     scene_root.free()
	## [/codeblock]
	func get_node_index() -> int:
		return _state.get_node_index(_idx) if is_valid() else -1

	## Returns the [SceneState] of the scene that this scene inherits from.
	func get_base_scene_state() -> SceneState:
		return _state.get_base_scene_state() if _state else null

	## Returns an [Array] of [Dictionary] items representing all signal connections originating from this node.
	## [br][br]
	## Each dictionary contains the following keys:
	## [br]- [code]"signal"[/code]: The [StringName] of the signal.
	## [br]- [code]"method"[/code]: The [StringName] of the connected method.
	## [br]- [code]"target"[/code]: The [NodePath] to the receiving node.
	## [br]- [code]"binds"[/code]: An [Array] of bound parameters.
	## [br]- [code]"unbinds"[/code]: An [int] representing the number of unbound parameters.
	## [br]- [code]"flags"[/code]: An [int] representing the connection flags (see [enum Object.ConnectFlags]).
	## [codeblock]
	## for connection in path.peek().get_connections():
	##     print("Signal: %s -> Method: %s" % [connection.signal, connection.method])
	## [/codeblock]
	func get_connections() -> Array[Dictionary]:
		var connections: Array[Dictionary] = []
		if is_valid():
			var clean_target := String(_state.get_node_path(_idx)).trim_prefix("./")
			if _idx == 0 or clean_target.is_empty(): clean_target = "."
			
			for c in _state.get_connection_count():
				var raw_source := String(_state.get_connection_source(c))
				var clean_source := raw_source.trim_prefix("./")
				if clean_source.is_empty(): clean_source = "."
				
				if clean_source == clean_target:
					connections.append({
						"signal": _state.get_connection_signal(c),
						"method": _state.get_connection_method(c),
						"target": _state.get_connection_target(c),
						"binds": _state.get_connection_binds(c),
						"unbinds": _state.get_connection_unbinds(c),
						"flags": _state.get_connection_flags(c)
					})
		return connections

## Internal helper to handle heavy SceneState lookups, timestamp caching, and deep sub-scene recursion.
class _StateCache:
	var state: SceneState
	var modified_time: int = 0

	func get_valid_state(raw_path: String) -> SceneState:
		if raw_path.is_empty(): return null
		var real_path := SceneNodePath._safe_resolve_path(raw_path)
		if real_path.is_empty() or not FileAccess.file_exists(real_path): return null

		var current_time := FileAccess.get_modified_time(real_path)
		if state and modified_time == current_time: return state

		var packed := load(real_path) as PackedScene
		if not packed: return null

		state = packed.get_state()
		modified_time = current_time
		return state

	## Recursively searches through SceneStates and Instanced Sub-Scenes to find the target node.
	## Returns an Array: [SceneState (where the node was found), int (the index)]
	func resolve_deep_node(root_state: SceneState, target_path: String) -> Array:
		if not root_state or target_path.is_empty(): 
			return [null, -1]
		
		var direct_idx = _find_idx_in_state(root_state, target_path)
		if direct_idx != -1:
			return [root_state, direct_idx]

		for i in root_state.get_node_count():
			var inst: PackedScene = root_state.get_node_instance(i)
			if inst:
				var inst_path := String(root_state.get_node_path(i)).trim_prefix("./")
				if inst_path == ".": continue
				
				# Check if our target path goes THROUGH this instance's path
				var prefix := inst_path + "/"
				if target_path.begins_with(prefix):
					var remainder := target_path.trim_prefix(prefix)
					var sub_state := inst.get_state()
					
					var result = resolve_deep_node(sub_state, remainder)
					if result[1] != -1:
						return result
		
		return [null, -1]

	func _find_idx_in_state(target_state: SceneState, current_path: String) -> int:
		var exact_np := NodePath(current_path)
		var relative_np := NodePath("./" + current_path)
		
		if current_path == "." or current_path == String(target_state.get_node_name(0)):
			return 0
			
		for i in target_state.get_node_count():
			var state_path := target_state.get_node_path(i)
			if state_path == exact_np or state_path == relative_np:
				return i
		return -1

var _cache := _StateCache.new()
