class_name SceneNodePathTest
extends GdUnitTestSuite


var debug_mode: bool = false


const PLAYER_SCENE: String = "uid://cskn5w06l6wjy"
const LEVEL_SCENE: String = "uid://cx70mn2ywq126"


func _xray(msg: String) -> void:
	if debug_mode:
		print_rich("[color=cyan][X-RAY][/color] ", msg)

func _create_path(scene: String, node: String) -> SceneNodePath:
	var path := SceneNodePath.new()
	path.scene_path = scene
	path.node_path = node
	return path

# ==========================================
# 1. Parsing & Validation Tests
# ==========================================

@warning_ignore("unused_parameter")
func test_init_parsing(formatted_string: String, expected_scene: String, expected_node: String, test_parameters := [
	["res://fake.tscn::Player", "res://fake.tscn", "Player"],
	["uid://fake_uid::Level/Boss", "uid://fake_uid", "Level/Boss"],
]) -> void:
	var path := SceneNodePath.new(formatted_string)
	_xray("Parsed string '%s' -> Scene: '%s', Node: '%s'" % [formatted_string, path.scene_path, path.node_path])
	assert_that(path.scene_path).is_equal(expected_scene)
	assert_that(path.node_path).is_equal(expected_node)

@warning_ignore("unused_parameter")
func test_validation_checks(scene: String, node: String, expect_surface: bool, expect_deep: bool, test_parameters := [
	[LEVEL_SCENE, "Player/Hitbox", true, true],
	[LEVEL_SCENE, ".", true, true],
	[LEVEL_SCENE, "FakeNode", true, false],
	["res://missing.tscn", "Hitbox", false, false],
	[LEVEL_SCENE, "", false, false]
]) -> void:
	var path := _create_path(scene, node)
	_xray("Validating: %s" % path.as_path())
	
	assert_that(path.is_valid()).is_equal(expect_surface)
	assert_that(path.peek().is_valid()).is_equal(expect_deep)



func test_state_inspector_basic_properties() -> void:
	var path := _create_path(PLAYER_SCENE, "Hitbox")
	var inspector = path.peek()
	
	_xray("Inspecting: %s" % path.as_path())
	_xray("  Type: %s" % inspector.get_node_type())
	_xray("  Monitoring: %s" % inspector.get_property("monitoring", true))
	
	assert_that(inspector.is_valid()).is_true()
	assert_that(inspector.get_node_type()).is_equal(&"Area2D")
	assert_that(inspector.get_property("monitoring", true)).is_equal(false)

func test_state_inspector_reads_subscene_properties_recursively() -> void:
	var path := _create_path(LEVEL_SCENE, "Player/Hitbox")
	var inspector = path.peek()
	
	_xray("Testing Deep Recursion on: %s" % path.as_path())
	
	assert_that(inspector.is_valid()).is_true()
	
	# Verify it can actually read the data from the sub-file
	assert_that(inspector.get_node_type()).is_equal(&"Area2D")
	assert_that(inspector.get_property("monitoring", true)).is_equal(false)

func test_state_inspector_reads_connections() -> void:
	var path := _create_path(PLAYER_SCENE, "Hitbox")
	var inspector = path.peek()
	var connections = inspector.get_connections()
	
	_xray("Found %d connections on %s" % [connections.size(), path.as_path()])
	
	assert_that(connections.size()).is_equal(1)
	if connections.is_empty(): return
	
	var conn = connections[0]
	_xray("  Connection: %s -> %s" % [conn["signal"], conn["method"]])
	
	assert_that(conn["signal"]).is_equal(&"tree_entered")
	assert_that(conn["method"]).is_equal(&"hide")

func test_state_inspector_identifies_instances() -> void:
	var path := _create_path(LEVEL_SCENE, "Player")
	var inspector = path.peek()
	
	var instance = inspector.get_node_instance()
	_xray("Checking if '%s' is an instance: %s" % [path.as_path(), instance != null])
	
	assert_that(instance).is_not_null()


func test_unwrap_into_tags_absolute_root() -> void:
	var parent := auto_free(Node.new())
	var path := _create_path(LEVEL_SCENE, "Player/Hitbox/DeepSecret")
	
	_xray("Unwrapping deep node: %s" % path.as_path())
	var target := path.unwrap_into(parent)
	
	assert_that(target).is_not_null()
	assert_that(target.name).is_equal(StringName("DeepSecret"))
	
	var tagged_root: Node = SceneNodePath.get_scene_root(target)
	_xray("  Target unwrap successful. Extracted absolute root tag: %s" % (tagged_root.name if tagged_root else "NULL"))
	
	assert_that(tagged_root).is_not_null()
	assert_that(tagged_root.name).is_equal(StringName("Level"))
	assert_that(tagged_root.get_parent()).is_same(parent)

func test_unwrap_into_or_null_handles_failures_safely() -> void:
	var parent := auto_free(Node.new())
	var path := _create_path("res://missing.tscn", "Hitbox")
	
	_xray("Attempting safe unwrap on missing scene: %s" % path.as_path())
	var spawned_node := path.unwrap_into_or_null(parent)
	
	assert_that(spawned_node).is_null()
	assert_that(parent.get_child_count()).is_equal(0)



func test_extract_isolates_nested_node_and_frees_parents() -> void:
	var path := _create_path(LEVEL_SCENE, "Player/Hitbox")
	
	_xray("Extracting and isolating node: %s" % path.as_path())
	var target := auto_free(path.extract())
	
	assert_that(target).is_not_null()
	assert_that(target.name).is_equal(StringName("Hitbox"))
	
	_xray("  Verifying extraction memory safety (parent & owner should be null)")
	assert_that(target.get_parent()).is_null()
	assert_that(target.owner).is_null()

func test_extract_root_node_returns_intact_scene() -> void:
	var path := _create_path(LEVEL_SCENE, ".")
	
	_xray("Extracting the root node directly: %s" % path.as_path())
	var target := auto_free(path.extract())
	
	assert_that(target).is_not_null()
	assert_that(target.name).is_equal(StringName("Level"))
	assert_that(target.is_queued_for_deletion()).is_false()

@warning_ignore("unused_parameter")
func test_extract_or_null_safe_failures(scene: String, node: String, test_parameters := [
	[LEVEL_SCENE, "FakeNode"],
	["res://missing.tscn", "Hitbox"],
	[LEVEL_SCENE, ""]
]) -> void:
	var path := _create_path(scene, node)
	_xray("Testing safe extraction failure on: %s" % path.as_path())
	assert_that(path.extract_or_null()).is_null()
