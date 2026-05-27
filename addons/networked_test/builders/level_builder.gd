## Fluent builder for programmatically composing game level scenes.
##
## Hard-wrapped to 80 columns.
class_name LevelBuilder
extends Object


## The unique name identifier for this level scene builder.
var scene_name: StringName
## The resource path assigned to the packed scene.
var resource_path: String = ""
## The compiled [PackedScene] after calling [method pack].
var packed: PackedScene = null

var _name: String
var _root_type: Variant = Node
var _has_spawner: bool = false
var _spawn_path: String = ".."
var _spawnable_scene_paths: Array[String] = []
var _custom_children: Array[Node] = []

static var _uid_counter: int = 0


# Initializes the level builder. If no name is provided, a unique sequential name is auto-generated.
func _init(level_name: String = "") -> void:
	if level_name.is_empty():
		_uid_counter += 1
		level_name = "AutogenLevel_%d" % _uid_counter
	_name = level_name
	scene_name = StringName(level_name)


## Resets the unique sequential name counter. Used in test teardown for determinism.
static func reset_counter() -> void:
	_uid_counter = 0

## Configures the level with a [MultiplayerSpawner].
##
## Accepts [param spawnables] as an array of [PackedScene] objects. Asserts
## that all packed scenes have a valid resource path.
func with_multiplayer_spawner(
	spawn_path: String = "..",
	spawnables: Array[PackedScene] = []
) -> LevelBuilder:
	_has_spawner = true
	_spawn_path = spawn_path
	for spawnable in spawnables:
		assert(
			not spawnable.resource_path.is_empty(),
			"LevelBuilder: spawnable PackedScene must have a valid resource path " + \
			"(take_over_path must have been called)."
		)
		_spawnable_scene_paths.append(spawnable.resource_path)
	return self


## Configures the custom root node class or script type.
func with_root(type: Variant) -> LevelBuilder:
	var dummy = type.new()
	assert(dummy is Node, "LevelBuilder: root type must inherit from Node.")
	dummy.free()
	_root_type = type
	return self


## Attaches a custom child [param node] to the level.
func with_child(node: Node) -> LevelBuilder:
	_custom_children.append(node)
	return self


## Composes and returns a live level node tree.
func build() -> Node:
	var root: Node = _root_type.new()
	root.name = _name
	
	if _has_spawner:
		var spawner: MultiplayerSpawner = MultiplayerSpawner.new()
		spawner.name = "PlayerSpawner"
		spawner.spawn_path = NodePath(_spawn_path)
		spawner.set("_spawnable_scenes", PackedStringArray(_spawnable_scene_paths))
		var _a1: Node = SceneAssembly.attach(root, spawner, root)
		
	for child in _custom_children:
		var child_dup: Node = child.duplicate()
		var _a2: Node = SceneAssembly.attach(root, child_dup, root)
		
	return root


## Composes, packs, and returns a [PackedScene] registered in memory.
func pack(custom_path: String = "") -> PackedScene:
	var root: Node = build()
	var path: String = custom_path if not custom_path.is_empty() else \
			NetwPathNamespace.next_path("level", _name)
	var p: PackedScene = SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(p)
	root.free()
	packed = p
	resource_path = path
	return p
