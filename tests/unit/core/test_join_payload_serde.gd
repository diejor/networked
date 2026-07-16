## Scalar round-trip tests for [JoinPayload].
##
## Covers the fields whose serialization is a pure value copy: username,
## peer_id, is_debug, and the wire-encoded join args ([member JoinPayload.arg_bytes]
## plus [member JoinPayload.schema_hash]).
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
			["valeria", 7, false],
			["jose", 0, true],
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
	original.username = &"valeria"

	var restored := _round_trip(original)
	assert_that(restored.is_debug).is_false()


func test_empty_args_round_trip_to_empty() -> void:
	# A payload with no join intent must deserialize into empty arg bytes and a
	# zero schema hash, not null and not a stale value.
	var original := JoinPayload.new()
	original.username = &"valeria"

	var restored := _round_trip(original)
	assert_that(restored.arg_bytes.is_empty()).is_true()
	assert_int(restored.schema_hash).is_equal(0)


func test_arg_bytes_and_hash_round_trip() -> void:
	# The encoded join args and their schema hash must survive
	# serialize/deserialize verbatim.
	var original := JoinPayload.new()
	original.username = &"valeria"
	original.arg_bytes = PackedByteArray([3, 1, 4, 1, 5, 9])
	original.schema_hash = 987654321

	var restored := _round_trip(original)
	assert_that(restored.arg_bytes).is_equal(PackedByteArray([3, 1, 4, 1, 5, 9]))
	assert_int(restored.schema_hash).is_equal(987654321)


func test_arg_values_are_transient() -> void:
	# arg_values is the live client-side input encoded by the session before
	# transmission, so it must not itself ride the wire.
	var original := JoinPayload.new()
	original.username = &"valeria"
	original.arg_values = [&"Level1", NodePath("Players/PlayerRoot")]

	var restored := _round_trip(original)
	assert_that(restored.arg_values).is_equal([])
