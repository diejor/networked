## Unit tests for [JoinPayload] serialization and deserialization.
class_name TestJoinPayloadSerde
extends NetworkedTestSuite


func test_round_trip_preserves_username() -> void:
	var original: JoinPayload = auto_free(JoinPayload.new())
	original.username = "alice"
	original.url = "localhost"
	original.peer_id = 7
	original.spawner_component_path = SceneNodePath.new()

	var bytes: PackedByteArray = original.serialize()
	assert_that(bytes.size()).is_greater(0)

	var restored: JoinPayload = auto_free(JoinPayload.new())
	restored.deserialize(bytes)

	assert_that(restored.username).is_equal(StringName("alice"))
	assert_that(restored.url).is_equal("localhost")
	assert_that(restored.peer_id).is_equal(7)


func test_round_trip_preserves_url() -> void:
	var data: JoinPayload = auto_free(JoinPayload.new())
	data.username = "bob"
	data.url = "ws://example.com:4433"
	data.peer_id = 0
	data.spawner_component_path = SceneNodePath.new()

	var bytes: PackedByteArray = data.serialize()
	var restored: JoinPayload = auto_free(JoinPayload.new())
	restored.deserialize(bytes)

	assert_that(restored.url).is_equal("ws://example.com:4433")


func test_serialize_returns_nonempty_bytes() -> void:
	var data: JoinPayload = auto_free(JoinPayload.new())
	data.username = "test"
	data.url = ""
	data.peer_id = 0
	data.spawner_component_path = SceneNodePath.new()

	assert_that(data.serialize().size()).is_greater(0)


func test_deserialize_does_not_crash_on_valid_bytes() -> void:
	var data: JoinPayload = auto_free(JoinPayload.new())
	data.username = "carol"
	data.url = "localhost"
	data.peer_id = 42
	data.spawner_component_path = SceneNodePath.new()

	var bytes: PackedByteArray = data.serialize()

	var restored: JoinPayload = auto_free(JoinPayload.new())
	# Must not push_error or crash
	restored.deserialize(bytes)
	assert_that(restored.username).is_equal(StringName("carol"))
