## The bytes a derived [NetwPropertySet] puts on the SYNC channel, and the
## per-peer books that decide what goes in them.
##
## Every case here asserts a hash, a frame's flags and fields, or a masked
## baseline and its ack, so a codec change that shortens a frame or a book
## change that diffs against an unconfirmed row fails here rather than in a
## capture. The derivation these sets come from, and the node writes they end
## in, are [code]tests/unit/sync/test_script_sync_sets.gd[/code].
##
## [method NetwPropertySet.wire_hash] carries the column order, the lane, the
## declared type and the quantizer, and never the schema name, so two peers
## whose scripts disagree on a shape poison the binding at spawn instead of
## decoding each other's bytes wrong.
class_name TestScriptSyncWire
extends NetwTestSuite

const STATE := NetwPropertySet.Record.RECORD_STATE
const INPUT := NetwPropertySet.Record.RECORD_INPUT

# Coarse enough that a fraction of one step is still far from the next code,
# so the grid laws below read as jitter versus motion rather than as rounding.
const _GRID_STEP := 0.5


func _prop() -> NetwScriptModel.PropertyConfig:
	var config := NetwScriptModel.PropertyConfig.new()
	config.is_property = true
	return config


func _make_set(fields: Array) -> NetwPropertySet:
	# Builds a set directly from [key, lane, type?, quantizer?] rows for
	# wire-hash coverage.
	var set := NetwPropertySet.new()
	for pair: Array in fields:
		var column := NetwPropertySet.Column.new(
			pair[0],
			pair[3] if pair.size() > 3 else null,
			false,
			pair[2] if pair.size() > 2 else SchemaCore.ColumnType.VARIANT,
		)
		column.lane = pair[1]
		set.bind(column)
	return set


func test_schema_hash_is_deterministic_and_16_bit() -> void:
	var a := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE], [&"hp", NetwPropertySet.Lane.RETAINED]])
	var b := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE], [&"hp", NetwPropertySet.Lane.RETAINED]])
	assert_int(a.wire_hash()).is_equal(b.wire_hash())
	assert_int(a.wire_hash()).is_between(0, 0xFFFF)


func test_schema_hash_is_sensitive_to_order_membership_and_lane() -> void:
	var base := _make_set([[&"a", NetwPropertySet.Lane.VOLATILE], [&"b", NetwPropertySet.Lane.VOLATILE]])
	var reordered := _make_set([[&"b", NetwPropertySet.Lane.VOLATILE], [&"a", NetwPropertySet.Lane.VOLATILE]])
	var extra := _make_set(
		[
			[&"a", NetwPropertySet.Lane.VOLATILE],
			[&"b", NetwPropertySet.Lane.VOLATILE],
			[&"c", NetwPropertySet.Lane.VOLATILE],
		],
	)
	var relaned := _make_set([[&"a", NetwPropertySet.Lane.VOLATILE], [&"b", NetwPropertySet.Lane.RETAINED]])
	assert_int(base.wire_hash()).is_not_equal(reordered.wire_hash())
	assert_int(base.wire_hash()).is_not_equal(extra.wire_hash())
	assert_int(base.wire_hash()).is_not_equal(relaned.wire_hash())


## Verify the declared column type is part of the wire hash, so two peers whose
## scripts type the same property differently poison the binding at spawn
## rather than decoding each other's bytes as the wrong shape.
func test_the_wire_hash_catches_a_type_disagreement() -> void:
	var floats := _make_set(
		[[&"pos", NetwPropertySet.Lane.VOLATILE, SchemaCore.ColumnType.F64]],
	)
	var vectors := _make_set(
		[[&"pos", NetwPropertySet.Lane.VOLATILE, SchemaCore.ColumnType.VECTOR3]],
	)

	assert_int(floats.wire_hash()).is_not_equal(vectors.wire_hash())


## Verify the quantizer is part of the wire hash, since two peers that packed
## one column to different bit widths cannot read each other. This is the hole
## a keys-and-lane hash could not catch.
func test_the_wire_hash_catches_a_quantizer_disagreement() -> void:
	var raw := _make_set(
		[[&"pos", NetwPropertySet.Lane.VOLATILE, SchemaCore.ColumnType.VECTOR3]],
	)
	var packed := _make_set(
		[
			[
				&"pos",
				NetwPropertySet.Lane.VOLATILE,
				SchemaCore.ColumnType.VECTOR3,
				NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
			],
		],
	)
	var coarser := _make_set(
		[
			[
				&"pos",
				NetwPropertySet.Lane.VOLATILE,
				SchemaCore.ColumnType.VECTOR3,
				NetwQuantizeFixed.new().step(0.5).limits(-512.0, 512.0),
			],
		],
	)

	assert_int(raw.wire_hash()).is_not_equal(packed.wire_hash())
	assert_int(packed.wire_hash()).is_not_equal(coarser.wire_hash())


## Verify the schema name never enters the wire hash, because a script with no
## resource path has no name two peers can agree on and a purely local fact
## must not poison a binding.
func test_the_wire_hash_ignores_the_schema_name() -> void:
	var here := _make_set([[&"pos", NetwPropertySet.Lane.VOLATILE]])
	var there := _make_set([[&"pos", NetwPropertySet.Lane.VOLATILE]])
	here.schema.name = &"res://a.gd"
	there.schema.name = &"@script:8811"

	assert_int(here.wire_hash()).is_equal(there.wire_hash())


func test_volatile_frame_round_trips_a_stamped_state_row() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(3, -4)
	src.rotation = 1.5
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK_ACK

	var bytes := NetwSyncPipeline.encode_volatile_frame(src, set, 2, 40, 37)
	assert_int(bytes.size()).is_greater(0)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncPipeline.apply_volatile_frame(dst, set, bytes)
	assert_int(header["ordinal"]).is_equal(2)
	assert_int(header["tick"]).is_equal(40)
	assert_int(header["ack"]).is_equal(37)
	assert_that(dst.position).is_equal(Vector2(3, -4))
	assert_float(dst.rotation).is_equal_approx(1.5, 0.0001)


func test_volatile_frame_skips_the_retained_lane() -> void:
	# A retained field rides SYNC_DELTA, never the volatile frame, so a node that
	# lacks it still round-trips the volatile row cleanly.
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(7, 8)
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"inventory", NetwPropertySet.Lane.RETAINED],
		],
	)
	var bytes := NetwSyncPipeline.encode_volatile_frame(src, set, 0, -1, -1)
	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	NetwSyncPipeline.apply_volatile_frame(dst, set, bytes)
	assert_that(dst.position).is_equal(Vector2(7, 8))


func test_volatile_frame_bails_on_a_missing_field() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var set := _make_set([[&"nonexistent", NetwPropertySet.Lane.VOLATILE]])
	assert_int(NetwSyncPipeline.encode_volatile_frame(src, set, 0, -1, -1).size()).is_equal(0)


func test_binding_encodes_a_windowed_input_ring() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK
	set.window = 3
	var binding := NetwPropertySetBinding.new(set, node)

	node.position = Vector2(1, 0)
	binding.encode_volatile(0, 10, -1)
	node.position = Vector2(2, 0)
	binding.encode_volatile(0, 11, -1)
	node.position = Vector2(3, 0)
	var bytes := binding.encode_volatile(0, 12, -1)

	var frame := NetwFrameEnvelope.decode_sync_frame(bytes, [null], [TYPE_VECTOR2])
	var samples: Array = frame["samples"]
	assert_int(frame["tick"]).is_equal(12)
	assert_int(samples.size()).is_equal(3)
	# Newest first: age 0 is the latest tick, the oldest is age 2.
	assert_int(samples[0][0]).is_equal(0)
	assert_that(samples[0][1][0]).is_equal(Vector2(3, 0))
	assert_int(samples[2][0]).is_equal(2)
	assert_that(samples[2][1][0]).is_equal(Vector2(1, 0))


func test_binding_retained_delta_heals_then_masks_changes() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 0.5
	var set := _make_set([[&"rotation", NetwPropertySet.Lane.RETAINED]])
	var binding := NetwPropertySetBinding.new(set, node)

	# A peer with no baseline heals with the full retained row.
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_greater(0)

	# No change since the peer's baseline sends nothing.
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_equal(0)

	# A changed retained field sends a delta again.
	node.rotation = 1.5
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_greater(0)


func test_binding_retained_delta_quantizes_configured_fields() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 1.0
	var codec := NetwQuantizeFixed.new()
	var set := NetwPropertySet.new()
	var field := NetwPropertySet.Column.new(&"rotation", codec)
	field.lane = NetwPropertySet.Lane.RETAINED
	set.bind(field)
	var binding := NetwPropertySetBinding.new(set, node)

	binding.poll_retained()
	var bytes := binding.retained_delta(0, 2)

	# Decode [ordinal | mask | value] with the same codec the encoder selected.
	var r := NetwBitBufferReader.create(bytes)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(0)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(1)
	var decoded := NetwScriptModel.read_values(r, [codec], [TYPE_FLOAT])
	assert_float(decoded[0]).is_equal_approx(1.0, 0.05)


func test_state_frame_carries_the_reconciliation_ack() -> void:
	# Wire lock: a state set frames STAMPED plus ACKED so the reconciliation ack
	# rides, and the no-input sentinel survives as -1 rather than a stray 0.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(2, 3)
	var set := NetwPropertySet.from_property_configs({ &"position": _prop().state() }, STATE)
	var binding := NetwPropertySetBinding.new(set, node)

	var frame := NetwFrameEnvelope.decode_sync_frame(
		binding.encode_volatile(0, 12, 9),
		[null],
		[TYPE_VECTOR2],
	)
	assert_int(frame["flags"]).is_equal(
		NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_ACKED,
	)
	assert_int(frame["tick"]).is_equal(12)
	assert_int(frame["ack"]).is_equal(9)

	var none := NetwFrameEnvelope.decode_sync_frame(
		binding.encode_volatile(0, 13, -1),
		[null],
		[TYPE_VECTOR2],
	)
	assert_int(none["ack"]).is_equal(-1)


func test_input_frame_carries_no_ack_slot() -> void:
	# Wire lock: an input set frames STAMPED | WINDOWED (the default input window
	# rides redundant samples), never ACKED, so an input row never reserves the
	# reconciliation-ack bytes a state row does.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 0.25
	var set := NetwPropertySet.from_property_configs({ &"rotation": _prop().input() }, INPUT)
	var frame := NetwFrameEnvelope.decode_sync_frame(
		NetwPropertySetBinding.new(set, node).encode_volatile(0, 7, 4),
		[null],
		[TYPE_FLOAT],
	)
	assert_int(frame["flags"]).is_equal(
		NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_WINDOWED,
	)
	assert_int(frame["ack"]).is_equal(-1)


func test_masked_delta_heals_an_unconfirmed_peer_with_the_full_row() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	# Peer 2 has never confirmed a baseline, so every field masks in.
	var first := binding.masked_delta(0, 2, 10, -1)
	assert_int((first["bytes"] as PackedByteArray).size()).is_greater(0)

	var frame := NetwFrameEnvelope.decode_sync_frame(first["bytes"], [null], [TYPE_VECTOR2])
	assert_int(frame["flags"] & NetwFrameEnvelope.SYNC_FLAG_MASKED).is_greater(0)
	assert_int(frame["mask"]).is_equal(1)


func test_masked_delta_sends_nothing_once_a_peer_is_confirmed_and_unchanged() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)

	# Same value, peer now confirmed: nothing to send.
	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_equal(0)


func test_masked_delta_never_advances_a_baseline_until_the_send_is_acked() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	# No ack yet: the peer's confirmed baseline is still absent, so the very
	# same value masks in again rather than silently going quiet, the
	# never-diff-against-an-unconfirmed-row guarantee.
	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_greater(0)

	binding.advance_masked_ack(2, 500)
	var third := binding.masked_delta(0, 2, 12, -1)
	assert_int((third["bytes"] as PackedByteArray).size()).is_equal(0)


func test_masked_delta_masks_only_the_changed_field() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	node.rotation = 0.5
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)

	node.rotation = 1.5
	var second := binding.masked_delta(0, 2, 11, -1)
	var frame := NetwFrameEnvelope.decode_sync_frame(
		second["bytes"],
		[null, null],
		[TYPE_VECTOR2, TYPE_FLOAT],
	)
	# Bit 1 (rotation) alone, position untouched.
	assert_int(frame["mask"]).is_equal(2)


## Sub-step jitter costs nothing, because the peer's confirmed baseline is what
## that peer decoded and the polled row is quantized to the same grid. A live
## value that wanders inside one grid step encodes to the byte the peer already
## holds, so masking it in would send those identical bytes again.
func test_masked_delta_ignores_a_change_inside_one_grid_step() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 1.0
	var set := _make_set(
		[
			[
				&"rotation",
				NetwPropertySet.Lane.VOLATILE,
				SchemaCore.ColumnType.F64,
				NetwQuantizeFixed.new().step(_GRID_STEP).limits(-512.0, 512.0),
			],
		],
	)
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)
	var confirmed: float = (first["row"] as Dictionary)[&"rotation"]

	node.rotation = confirmed + _GRID_STEP * 0.4
	var jittered := binding.masked_delta(0, 2, 11, -1)
	assert_int((jittered["bytes"] as PackedByteArray).size()).is_equal(0)


## A change that crosses to the next code does mask in, so the fix that silences
## jitter is not silencing motion.
func test_masked_delta_sends_a_change_that_crosses_a_grid_step() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 1.0
	var set := _make_set(
		[
			[
				&"rotation",
				NetwPropertySet.Lane.VOLATILE,
				SchemaCore.ColumnType.F64,
				NetwQuantizeFixed.new().step(_GRID_STEP).limits(-512.0, 512.0),
			],
		],
	)
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)
	var confirmed: float = (first["row"] as Dictionary)[&"rotation"]

	node.rotation = confirmed + _GRID_STEP
	var moved := binding.masked_delta(0, 2, 11, -1)
	assert_int((moved["bytes"] as PackedByteArray).size()).is_greater(0)
	var frame := NetwFrameEnvelope.decode_sync_frame(
		moved["bytes"],
		[null],
		[TYPE_FLOAT],
	)
	assert_int(frame["mask"]).is_equal(1)


func test_masked_apply_merges_a_partial_frame_onto_the_last_decoded_row() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(1, 2)
	src.rotation = 0.5
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	set.masked = true
	var src_binding := NetwPropertySetBinding.new(set, src)
	var full := src_binding.masked_delta(0, 9, 10, -1) # gain edge, full row

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	dst_binding.apply_volatile(full["bytes"])
	assert_that(dst.position).is_equal(Vector2(1, 2))
	assert_float(dst.rotation).is_equal_approx(0.5, 0.0001)

	src_binding.commit_masked_pending(9, 500, full["row"])
	src_binding.advance_masked_ack(9, 500)
	src.rotation = 1.5
	var partial := src_binding.masked_delta(0, 9, 11, -1)

	var header := dst_binding.apply_volatile(partial["bytes"])
	# Only rotation was masked in, but position must survive the merge.
	assert_that(dst.position).is_equal(Vector2(1, 2))
	assert_float(dst.rotation).is_equal_approx(1.5, 0.0001)
	assert_float((header["payload"] as Dictionary)[&"rotation"]).is_equal_approx(1.5, 0.0001)
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(1, 2))


func test_masked_apply_merges_against_last_row_when_write_gate_is_off() -> void:
	# A reconciling client (write_gate false) must merge a masked frame against
	# its own last-decoded authoritative row, not the live node, which may hold
	# a diverging prediction.
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(1, 2)
	src.rotation = 0.5
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	set.masked = true
	var src_binding := NetwPropertySetBinding.new(set, src)
	var full := src_binding.masked_delta(0, 9, 10, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	dst.position = Vector2(99, 99) # a diverging prediction
	dst.rotation = 9.0
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	dst_binding.write_gate = false
	dst_binding.apply_volatile(full["bytes"])
	# write_gate false: the node never snaps.
	assert_that(dst.position).is_equal(Vector2(99, 99))

	src_binding.commit_masked_pending(9, 500, full["row"])
	src_binding.advance_masked_ack(9, 500)
	src.rotation = 1.5
	var partial := src_binding.masked_delta(0, 9, 11, -1)
	var header := dst_binding.apply_volatile(partial["bytes"])
	# The merged row reflects the authoritative position from the first frame,
	# not the diverging prediction the (unwritten) node still holds.
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(1, 2))
	assert_float((header["payload"] as Dictionary)[&"rotation"]).is_equal_approx(1.5, 0.0001)


func test_advance_masked_ack_no_ops_on_a_peer_with_no_in_flight_rows() -> void:
	# note_peer_ack fans out to every derived binding regardless of whether that
	# binding's set is masked or ever sent this peer anything, so an untyped
	# Dictionary.get(peer) with no default (returning Nil into a Dictionary-typed
	# local) must never be reached here.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)
	binding.advance_masked_ack(2, 500) # must not error


func test_commit_pending_masked_no_ops_when_nothing_was_staged() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	pipeline.commit_pending_masked(2, 500) # must not error
	pipeline.dispose()


func test_retain_masked_baselines_drops_a_peer_no_longer_a_recipient() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwPropertySetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)
	# Peer 2 no longer admitted: its baseline must drop, so a later re-admission
	# heals with a full row instead of diffing against the stale one.
	binding.retain_masked_baselines([])

	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_greater(0)
	var frame := NetwFrameEnvelope.decode_sync_frame(second["bytes"], [null], [TYPE_VECTOR2])
	assert_int(frame["mask"]).is_equal(1)
