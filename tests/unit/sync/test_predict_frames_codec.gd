## Wire-first laws for the prediction COMMAND and ACK lanes.
##
## The COMMAND layout carries one rule the rest of the family depends on: a
## fresh transition and its command are inseparable, so a frame missing a
## payload has no shorter reading and is dropped whole. These cases pin that by
## hand-building the exact bytes each frame frames, the way
## [code]test_sync_frame_codec.gd[/code] pins the sync grammar, so a codec
## change that silently shortens a frame fails here rather than in a capture.
class_name TestPredictFramesCodec
extends NetwTestSuite

const PredictFrames := NetwLagCompensationInterface._PredictFrames

const EPOCH := 7
const ACK_OF_ACKS := 12


func _rows(specs: Array) -> Array:
	# [index, label, fresh] triples into the contiguous transition rows.
	var out: Array = []
	for s: Array in specs:
		out.append({ "index": s[0], "label": s[1], "fresh": s[2] })
	return out


func test_command_roundtrips_transitions_and_their_payloads() -> void:
	var rows := _rows([[4, 100, true], [5, 100, false], [6, 102, true]])
	var bytes := PredictFrames.encode_command(
		EPOCH,
		ACK_OF_ACKS,
		rows,
		[[Vector2(1, 0)], [Vector2(0, 1)]],
		[null],
		[TYPE_VECTOR2],
	)

	var frame := PredictFrames.decode_command(bytes, [null], [TYPE_VECTOR2])

	assert_int(frame["epoch"]).is_equal(EPOCH)
	assert_int(frame["ack_of_acks"]).is_equal(ACK_OF_ACKS)
	assert_array(frame["transitions"]).is_equal(rows)
	assert_int((frame["payloads"] as Array).size()).override_failure_message(
		"one payload per fresh transition, never per transition",
	).is_equal(2)
	assert_that(frame["payloads"][0][0]).is_equal(Vector2(1, 0))
	assert_that(frame["payloads"][1][0]).is_equal(Vector2(0, 1))


func test_command_with_no_fresh_transition_carries_no_payload() -> void:
	var rows := _rows([[9, 40, false], [10, 40, false]])
	var bytes := PredictFrames.encode_command(
		EPOCH,
		0,
		rows,
		[],
		[null],
		[TYPE_VECTOR2],
	)

	var frame := PredictFrames.decode_command(bytes, [null], [TYPE_VECTOR2])

	assert_array(frame["transitions"]).is_equal(rows)
	assert_array(frame["payloads"]).is_empty()


func test_command_missing_a_payload_drops_the_whole_frame() -> void:
	var rows := _rows([[0, 10, true], [1, 11, true]])
	var honest := PredictFrames.encode_command(
		EPOCH,
		0,
		rows,
		[[Vector2(1, 0)], [Vector2(0, 1)]],
		[null],
		[TYPE_VECTOR2],
	)
	# Same header, one payload short. This is the frame the old grammar would
	# have parsed into a transition authority then substituted a command for.
	var truncated := PredictFrames.encode_command(
		EPOCH,
		0,
		rows,
		[[Vector2(1, 0)]],
		[null],
		[TYPE_VECTOR2],
	)

	assert_dict(PredictFrames.decode_command(honest, [null], [TYPE_VECTOR2])) \
			.is_not_empty()
	assert_dict(
		PredictFrames.decode_command(truncated, [null], [TYPE_VECTOR2]),
	).override_failure_message(
		"a fresh transition without its command has no honest reading",
	).is_empty()


func test_command_pins_its_header_bytes() -> void:
	var rows := _rows([[3, 20, true]])
	var bytes := PredictFrames.encode_command(
		1,
		2,
		rows,
		[[Vector2.ZERO]],
		[null],
		[TYPE_VECTOR2],
	)

	# zigzag(ack_of_acks) varint, epoch u8, base varint, count u8, then the
	# tagged label delta: zigzag(20 - 0) << 1 | fresh == 40 << 1 | 1 == 81.
	assert_int(bytes[0]).is_equal(4)
	assert_int(bytes[1]).is_equal(1)
	assert_int(bytes[2]).is_equal(3)
	assert_int(bytes[3]).is_equal(1)
	assert_int(bytes[4]).override_failure_message(
		"the transition bits are the landed tape codec, unchanged",
	).is_equal(81)


func test_command_carries_the_unset_acknowledgement_frontier() -> void:
	# The frontier is -1 until the first acknowledgement lands. Carried as a
	# plain varint it would arrive as a huge positive and permanently floor
	# authority's re-send window past every transition it could acknowledge,
	# so the lane would go silent exactly when it had never spoken.
	var rows := _rows([[0, 5, true]])
	var frame := PredictFrames.decode_command(
		PredictFrames.encode_command(
			EPOCH, -1, rows, [[Vector2.ZERO]], [null], [TYPE_VECTOR2],
		),
		[null],
		[TYPE_VECTOR2],
	)

	assert_int(frame["ack_of_acks"]).override_failure_message(
		"an unset frontier must arrive unset",
	).is_equal(-1)


func test_command_fp_section_is_optional() -> void:
	var rows := _rows([[0, 5, true]])
	var bare := PredictFrames.decode_command(
		PredictFrames.encode_command(
			EPOCH, 0, rows, [[Vector2.ZERO]], [null], [TYPE_VECTOR2],
		),
		[null],
		[TYPE_VECTOR2],
	)
	assert_array(bare["post_fps"]).is_empty()
	assert_array(bare["e_digests"]).is_empty()

	var carried := PredictFrames.decode_command(
		PredictFrames.encode_command(
			EPOCH,
			0,
			rows,
			[[Vector2.ZERO]],
			[null],
			[TYPE_VECTOR2],
			PackedInt32Array([-2147483648, 0, 2147483647]),
			PackedInt32Array([13, -14, 15]),
		),
		[null],
		[TYPE_VECTOR2],
	)
	assert_array(carried["post_fps"]).override_failure_message(
		"a fingerprint must survive the u32 crossing unchanged",
	).is_equal(PackedInt32Array([-2147483648, 0, 2147483647]))
	# The digest rides beside the fingerprint because authority cannot separate a
	# world it disagrees about from a step it disagrees about without it, so a
	# section that carried one and lost the other would report the wrong cause.
	assert_array(carried["e_digests"]).override_failure_message(
		"the environment digest must survive the crossing beside its fingerprint",
	).is_equal(PackedInt32Array([13, -14, 15]))


# The two columns describe one transition each, so a caller that supplies only
# one of them has described no transition completely. The section carries the
# pairs it has rather than inventing a digest to sit beside a fingerprint.
func test_the_fp_section_carries_only_complete_pairs() -> void:
	var rows := _rows([[0, 5, true]])
	var lopsided := PredictFrames.decode_command(
		PredictFrames.encode_command(
			EPOCH,
			0,
			rows,
			[[Vector2.ZERO]],
			[null],
			[TYPE_VECTOR2],
			PackedInt32Array([11, 22, 33]),
			PackedInt32Array([44]),
		),
		[null],
		[TYPE_VECTOR2],
	)
	assert_array(lopsided["post_fps"]).is_equal(PackedInt32Array([11]))
	assert_array(lopsided["e_digests"]).is_equal(PackedInt32Array([44]))


func test_ack_roundtrips_its_columns() -> void:
	var bytes := PredictFrames.encode_ack(
		30,
		PackedInt32Array([111, -222, 333]),
		PackedInt32Array([7, 8, 9]),
		PackedInt32Array([-1, 2147483647, 0]),
		PackedByteArray([0, PredictFrames.ACK_SUBSTITUTED, 0]),
	)

	var frame := PredictFrames.decode_ack(bytes)

	assert_int(frame["base"]).is_equal(30)
	assert_array(frame["c_hashes"]).is_equal(PackedInt32Array([111, -222, 333]))
	assert_array(frame["e_digests"]).is_equal(PackedInt32Array([7, 8, 9]))
	assert_array(frame["post_fps"]) \
			.is_equal(PackedInt32Array([-1, 2147483647, 0]))
	assert_array(frame["flags"]) \
			.is_equal(PackedByteArray([0, PredictFrames.ACK_SUBSTITUTED, 0]))


# Attribution is only honest when the owner can test the antecedents against
# authority's own values instead of assuming them, so the ack carries the
# environment digest beside the command hash. Without it every divergence would
# fall through to the simulation and the charge would mean nothing.
func test_ack_carries_the_antecedents_a_divergence_is_charged_to() -> void:
	var frame := PredictFrames.decode_ack(PredictFrames.encode_ack(
		0,
		PackedInt32Array([111]),
		PackedInt32Array([-2147483648]),
		PackedInt32Array([222]),
		PackedByteArray([0]),
	))

	assert_array(frame["e_digests"]).override_failure_message(
		"a digest must survive the u32 crossing unchanged",
	).is_equal(PackedInt32Array([-2147483648]))


func test_ack_declares_substitution_per_transition() -> void:
	var bytes := PredictFrames.encode_ack(
		0,
		PackedInt32Array([1, 2]),
		PackedInt32Array([5, 6]),
		PackedInt32Array([3, 4]),
		PackedByteArray([0, PredictFrames.ACK_SUBSTITUTED]),
	)

	var frame := PredictFrames.decode_ack(bytes)
	var flags: PackedByteArray = frame["flags"]

	assert_bool(flags[0] & PredictFrames.ACK_SUBSTITUTED > 0).is_false()
	assert_bool(flags[1] & PredictFrames.ACK_SUBSTITUTED > 0) \
			.override_failure_message(
				"an owner learns of substitution from the ack, never by "
				+ "inferring it from a state it cannot explain",
			).is_true()


func test_a_truncated_or_empty_frame_decodes_to_nothing() -> void:
	assert_dict(PredictFrames.decode_ack(PackedByteArray())).is_empty()
	assert_dict(
		PredictFrames.decode_command(PackedByteArray(), [null], [TYPE_VECTOR2]),
	).is_empty()
