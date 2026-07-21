## Round-trip tests for the [constant NetwFrameEnvelope.Channel.SYNC] frame codec,
## the shared encoder and decoder every consumed, state, and input set frames its
## payload through. The wire-first flags-grammar suite pins the byte layout by
## hand; this suite proves [method NetwFrameEnvelope.encode_sync_frame] emits
## exactly that layout and [method NetwFrameEnvelope.decode_sync_frame] reads it
## back losslessly across every flag variant.
class_name TestSyncFrameCodec
extends NetwTestSuite

const STAMPED := NetwFrameEnvelope.SYNC_FLAG_STAMPED
const ACKED := NetwFrameEnvelope.SYNC_FLAG_ACKED
const WINDOWED := NetwFrameEnvelope.SYNC_FLAG_WINDOWED
const TAPED := NetwFrameEnvelope.SYNC_FLAG_TAPED
const MASKED := NetwFrameEnvelope.SYNC_FLAG_MASKED


func test_plain_stamped_and_acked_round_trip() -> void:
	# The scalar-header variants (plain, stamped, stamped+acked) carry the whole
	# field row after the header, and the ack rides value-plus-one so the
	# no-input sentinel -1 survives an unsigned varint.
	var cases: Array = [
		{ "flags": 0, "ordinal": 0, "tick": -1, "ack": -1, "values": [55] },
		{ "flags": STAMPED, "ordinal": 2, "tick": 4096, "ack": -1, "values": [12, "hp"] },
		{ "flags": STAMPED | ACKED, "ordinal": 1, "tick": 7, "ack": -1, "values": [3.5] },
		{ "flags": STAMPED | ACKED, "ordinal": 1, "tick": 7, "ack": 0, "values": [3.5] },
		{ "flags": STAMPED | ACKED, "ordinal": 9, "tick": 30, "ack": 41, "values": [true, 9] },
	]
	for c: Dictionary in cases:
		var bytes := NetwFrameEnvelope.encode_sync_frame(c)
		var got := NetwFrameEnvelope.decode_sync_frame(bytes, [], [])
		assert_int(got["ordinal"]).is_equal(c["ordinal"])
		assert_int(got["flags"]).is_equal(c["flags"])
		assert_int(got["tick"]).is_equal(c["tick"])
		assert_int(got["ack"]).is_equal(c["ack"])
		assert_array(got["values"]).is_equal(c["values"])


func test_windowed_frame_round_trips_age_rows_newest_first() -> void:
	# The windowed variant carries a sample count then one age-prefixed row per
	# sample, newest at age zero, in place of a single value row.
	var samples: Array = [[0, [30]], [1, [20]], [3, [10]]]
	var bytes := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": STAMPED | WINDOWED,
			"ordinal": 0,
			"tick": 9,
			"samples": samples,
		},
	)
	var got := NetwFrameEnvelope.decode_sync_frame(bytes, [], [])
	assert_int(got["flags"]).is_equal(STAMPED | WINDOWED)
	assert_int(got["tick"]).is_equal(9)
	assert_array(got["samples"]).is_equal(samples)


func test_taped_window_round_trips_epoch_labels_and_repeat_entries() -> void:
	var entries: Array = [
		{ "index": 40, "label": 100, "fresh": true },
		{ "index": 41, "label": 100, "fresh": false },
		{ "index": 42, "label": 102, "fresh": true },
	]
	var bytes := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": STAMPED | WINDOWED | TAPED,
			"ordinal": 0,
			"tick": 102,
			"samples": [[0, [30]], [2, [10]]],
			"tape": { "epoch": 7, "entries": entries },
		},
	)
	var got := NetwFrameEnvelope.decode_sync_frame(bytes, [], [])
	assert_int(got["flags"]).is_equal(STAMPED | WINDOWED | TAPED)
	assert_int(got["tape_epoch"]).is_equal(7)
	assert_array(got["entries"]).is_equal(entries)
	var plain := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": STAMPED | WINDOWED,
			"ordinal": 0,
			"tick": 102,
			"samples": [[0, [30]], [2, [10]]],
		},
	)
	assert_int(bytes.size() - plain.size()).is_equal(7)


func test_masked_frame_selects_subset_against_full_schema() -> void:
	# The masked variant names its fields with a bitmask and carries only the set
	# bits' values, decoded against the full set's parallel arrays.
	var full_quantizers: Array = [null, null, null, null]
	var full_types: Array = [TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT]
	# Fields 0 and 2 changed: mask 0b0101 = 5, values in ascending bit order.
	var bytes := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": STAMPED | MASKED,
			"ordinal": 4,
			"tick": 12,
			"mask": 5,
			"values": [100, 300],
			"quantizers": [null, null],
			"types": [TYPE_INT, TYPE_INT],
		},
	)
	var got := NetwFrameEnvelope.decode_sync_frame(bytes, full_quantizers, full_types)
	assert_int(got["ordinal"]).is_equal(4)
	assert_int(got["tick"]).is_equal(12)
	assert_int(got["mask"]).is_equal(5)
	assert_array(got["indices"]).is_equal([0, 2])
	assert_array(got["values"]).is_equal([100, 300])


func test_encoded_bytes_match_the_pinned_hand_built_layout() -> void:
	# The codec must emit the exact bytes the wire-first flags-grammar suite pins
	# by hand, field for field, so both peers and the decoder agree byte for byte.
	var bytes := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": STAMPED | ACKED,
			"ordinal": 2,
			"tick": 7,
			"ack": 41,
			"values": [12],
		},
	)
	var r := NetwBitBuffer.Reader.new(bytes)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(2) # ordinal
	assert_int(r.get_aligned_u8()).is_equal(STAMPED | ACKED) # flags
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(7) # tick
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(42) # ack value-plus-one
	assert_array(NetwScriptModel.read_values(r, [], [])).is_equal([12])


func test_plain_frame_is_byte_identical_to_a_bare_positional_payload() -> void:
	# A flags-zero frame is exactly the pre-convergence consumed payload
	# (ordinal, zero flags, positional values), which is what makes the flags
	# byte an extension point rather than a re-cut.
	var hand := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(hand, 3)
	hand.put_aligned_u8(0)
	NetwScriptModel.write_values(hand, [7, 8], [], [TYPE_INT, TYPE_INT])

	var coded := NetwFrameEnvelope.encode_sync_frame(
		{
			"flags": 0,
			"ordinal": 3,
			"values": [7, 8],
		},
	)
	assert_array(coded).is_equal(hand.to_bytes())
