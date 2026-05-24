## Unit tests for [JoinPayload] serialization and deserialization.
class_name TestJoinPayloadSerde
extends NetwTestSuite


func test_round_trip_preserves_payload(
	username: String,
	url: String,
	peer_id: int,
	is_debug: bool,
	test_parameters := [
		["alice", "localhost", 7, false],
		["bob", "ws://example.com:4433", 0, true],
		["carol", "", 42, false],
		["", "127.0.0.1", -1, true],
	]
) -> void:
	var original: JoinPayload = auto_free(JoinPayload.new())
	original.username = StringName(username)
	original.url = url
	original.peer_id = peer_id
	original.is_debug = is_debug
	original.spawner_component_path = SceneNodePath.new()

	var bytes: PackedByteArray = original.serialize()
	assert_that(bytes.size()).is_greater(0)

	var restored: JoinPayload = auto_free(JoinPayload.new())
	restored.deserialize(bytes)

	assert_that(restored.username).is_equal(StringName(username))
	assert_that(restored.url).is_equal(url)
	assert_that(restored.peer_id).is_equal(peer_id)
	assert_that(restored.is_debug).is_equal(is_debug)
