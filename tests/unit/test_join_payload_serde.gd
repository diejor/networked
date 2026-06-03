## Scalar round-trip tests for [JoinPayload].
##
## Covers the fields whose serialization is a pure value copy: username,
## peer_id, is_debug, and the opaque [member JoinPayload.spawn] dictionary.
class_name TestJoinPayloadSerde
extends NetwTestSuite

func _round_trip(original: JoinPayload) -> JoinPayload:
	var restored := JoinPayload.new()
	restored.deserialize(original.serialize())
	return restored


@warning_ignore("unused_parameter")
func test_round_trip_preserves_scalars(
		username: String,
		peer_id: int,
		is_debug: bool,
		test_parameters := [
			["alice", 7, false],
			["bob", 0, true],
			["carol", 42, false],
			["", -1, true],
		],
) -> void:
	var original := JoinPayload.new()
	original.username = StringName(username)
	original.peer_id = peer_id
	original.is_debug = is_debug

	var restored := _round_trip(original)

	assert_that(restored.username).is_equal(StringName(username))
	assert_that(restored.peer_id).is_equal(peer_id)
	assert_that(restored.is_debug).is_equal(is_debug)


func test_default_is_debug_is_false() -> void:
	# Round-tripping a payload that omits is_debug must read back false,
	# not null. Guards against the [code]data.get("is_debug", false)[/code]
	# default in [method JoinPayload.deserialize].
	var original := JoinPayload.new()
	original.username = &"alice"

	var restored := _round_trip(original)
	assert_that(restored.is_debug).is_false()


func test_empty_spawn_round_trips_to_empty() -> void:
	# A payload with no spawn intent must deserialize into an empty
	# dictionary, not null and not a stale value.
	var original := JoinPayload.new()
	original.username = &"alice"

	var restored := _round_trip(original)
	assert_that(restored.spawn).is_equal({ })


func test_spawn_dict_round_trips() -> void:
	# The opaque spawn dictionary (a SpawnPolicy.to_dict payload) must survive
	# serialize/deserialize verbatim, including StringName and NodePath values.
	var original := JoinPayload.new()
	original.username = &"alice"
	original.spawn = SpawnerComponentPolicy.from_scene_node_path(
		_spawner_path(&"Level1", "Players/SpawnerComponent"),
	).to_dict()

	var restored := _round_trip(original)
	assert_that(StringName(restored.spawn.get("scene_name"))).is_equal(&"Level1")
	assert_that(restored.spawn.get("spawner_path")) \
			.is_equal(NodePath("Players/SpawnerComponent"))


func _spawner_path(scene_name: StringName, node_path: String) -> SceneNodePath:
	# from_scene_node_path reads get_scene_name()/node_path; a UID-less path is
	# enough to exercise the dictionary round trip without touching disk.
	var snp := SceneNodePath.new()
	snp.scene_path = "res://%s.tscn" % scene_name
	snp.node_path = node_path
	return snp
