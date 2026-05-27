## Scalar round-trip tests for [JoinPayload].
##
## Covers the fields whose serialization is a pure value copy: username,
## url, peer_id, is_debug. The spawner_component_path contract lives in
## [code]test_join_payload_path_normalization.gd[/code] because it is not a
## value copy, the path is canonicalized to UID form on serialize.
class_name TestJoinPayloadSerde
extends NetwTestSuite


func _round_trip(original: JoinPayload) -> JoinPayload:
	var restored := JoinPayload.new()
	restored.deserialize(original.serialize())
	return restored


@warning_ignore("unused_parameter")
func test_round_trip_preserves_scalars(
	username: String,
	url: String,
	peer_id: int,
	is_debug: bool,
	test_parameters := [
		["alice", "localhost",         7,  false],
		["bob",   "ws://example:4433", 0,  true],
		["carol", "",                  42, false],
		["",      "127.0.0.1",         -1, true],
	],
) -> void:
	var original := JoinPayload.new()
	original.username = StringName(username)
	original.url = url
	original.peer_id = peer_id
	original.is_debug = is_debug

	var restored := _round_trip(original)

	assert_that(restored.username).is_equal(StringName(username))
	assert_that(restored.url).is_equal(url)
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


func test_empty_spawner_path_round_trips_to_empty() -> void:
	# Serializing a payload with no spawner_component_path must produce a
	# payload that deserializes into an empty SceneNodePath, not crash and
	# not carry a stale path forward.
	var original := JoinPayload.new()
	original.username = &"alice"

	var restored := _round_trip(original)
	assert_that(restored.spawner_component_path).is_not_null()
	assert_that(restored.spawner_component_path.scene_path).is_equal("")
	assert_that(restored.spawner_component_path.node_path).is_equal("")
