## Contract tests for how [JoinPayload] canonicalizes
## [member JoinPayload.spawner_component_path] across the
## serialize/deserialize boundary.
##
## [JoinPayload.serialize] calls [method SceneNodePath.as_uid], which
## promotes [code]res://[/code] paths to [code]uid://[/code] form whenever
## the resource is loadable. Tests that ship a payload over the wire need
## to know which form to expect on the other side.
class_name TestJoinPayloadPathNormalization
extends NetwTestSuite

const _LEVEL: PackedScene = preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)
const _NODE_PATH := "TestPlayerMinimal/SpawnerComponent"


func _payload_with_path(scene_path: String, node_path: String) -> JoinPayload:
	var p := JoinPayload.new()
	p.username = &"alice"
	p.spawner_component_path = SceneNodePath.new()
	p.spawner_component_path.scene_path = scene_path
	p.spawner_component_path.node_path = node_path
	return p


func _round_trip(original: JoinPayload) -> JoinPayload:
	var restored := JoinPayload.new()
	restored.deserialize(original.serialize())
	return restored


func test_res_path_with_uid_promotes_to_uid_form() -> void:
	# A loadable res:// path must come back as uid:// after a round trip,
	# because clients should never depend on a path that can move on disk.
	var original := _payload_with_path(_LEVEL.resource_path, _NODE_PATH)
	var restored := _round_trip(original)

	assert_that(restored.spawner_component_path.scene_path.begins_with("uid://")) \
		.is_true()
	assert_that(restored.spawner_component_path.node_path).is_equal(_NODE_PATH)


func test_uid_path_round_trips_unchanged() -> void:
	# A path already in uid:// form must come back identical.
	var uid_form := SceneNodePath.new()
	uid_form.scene_path = _LEVEL.resource_path
	var canonical_uid := uid_form.as_uid().split("::")[0]

	var original := _payload_with_path(canonical_uid, _NODE_PATH)
	var restored := _round_trip(original)

	assert_that(restored.spawner_component_path.scene_path).is_equal(canonical_uid)
	assert_that(restored.spawner_component_path.node_path).is_equal(_NODE_PATH)


func test_get_scene_name_is_stable_across_round_trip() -> void:
	# The derived scene_name is what the join flow uses to route the player
	# into a scene. It must be identical before and after serialize.
	var original := _payload_with_path(_LEVEL.resource_path, _NODE_PATH)
	var restored := _round_trip(original)

	assert_that(restored.spawner_component_path.get_scene_name()) \
		.is_equal(original.spawner_component_path.get_scene_name())
	assert_that(restored.spawner_component_path.get_scene_name()) \
		.is_equal("TestLevel")
