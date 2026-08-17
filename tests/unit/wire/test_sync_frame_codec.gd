## Round-trip tests for the [constant NetwFrameEnvelope.Channel.SYNC] frame
## codec, the payload a consumed [MultiplayerSynchronizer] frames its row
## through. The wire-first reserved-byte suite pins the layout by hand; this
## suite proves [method NetwSyncKernel.encode_volatile] emits exactly that
## layout and [method NetwSyncKernel.decode_volatile] reads it back losslessly.
class_name TestSyncFrameCodec
extends NetwTestSuite


func test_volatile_row_round_trips_under_its_keys() -> void:
	# The values self-describe, so a row of mixed types lands under the caller's
	# keys with no schema on either side.
	var bytes := NetwSyncKernel.encode_volatile(9, [true, 9, "hp", 3.5])
	var staged := NetwSyncKernel.decode_volatile(
		bytes,
		[&"alive", &"count", &"label", &"rate"],
	)
	assert_object(staged).is_not_null()
	assert_int(staged.ordinal).is_equal(9)
	assert_array(staged.values).is_equal([true, 9, "hp", 3.5])
	assert_dict(staged.row).is_equal(
		{ &"alive": true, &"count": 9, &"label": "hp", &"rate": 3.5 },
	)


func test_a_written_reserved_byte_is_refused_rather_than_read() -> void:
	# The values that follow were framed under a grammar this version does not
	# have, so there is no reading of them that is better than none.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(1)
	NetwCodec.write_values(w, [55], [], [TYPE_INT])

	assert_object(NetwSyncKernel.decode_volatile(w.to_bytes(), [&"synced"])) \
		.is_null()


func test_a_row_that_does_not_fill_its_keys_is_refused() -> void:
	var bytes := NetwSyncKernel.encode_volatile(0, [1, 2])

	assert_object(NetwSyncKernel.decode_volatile(bytes, [&"a", &"b", &"c"])) \
		.is_null()


func test_encoded_bytes_match_the_pinned_hand_built_layout() -> void:
	# The codec must emit the exact bytes the wire-first suite pins by hand,
	# field for field, so both peers and the decoder agree byte for byte.
	var bytes := NetwSyncKernel.encode_volatile(2, [12])

	var r := NetwBitBufferReader.create(bytes)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(2) # ordinal
	assert_int(r.get_aligned_u8()).is_equal(NetwSyncKernel.RESERVED)
	assert_array(NetwCodec.read_values(r, [], [])).is_equal([12])


func test_retained_row_round_trips_the_keys_its_mask_names() -> void:
	# Fields 0 and 2 changed: mask 0b0101 = 5, values in ascending bit order.
	var bytes := NetwSyncKernel.encode_retained(4, 5, [100, 300])
	var staged := NetwSyncKernel.decode_retained(bytes, [&"a", &"b", &"c", &"d"])

	assert_object(staged).is_not_null()
	assert_int(staged.ordinal).is_equal(4)
	assert_array(staged.keys).is_equal([&"a", &"c"])
	assert_dict(staged.row).is_equal({ &"a": 100, &"c": 300 })


func test_a_retained_row_that_disagrees_with_its_mask_is_refused() -> void:
	var bytes := NetwSyncKernel.encode_retained(0, 0b111, [1, 2])

	assert_object(NetwSyncKernel.decode_retained(bytes, [&"a", &"b", &"c"])) \
		.is_null()
