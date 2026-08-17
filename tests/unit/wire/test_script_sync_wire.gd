## The shape a derived [NetwPropertySet] agrees on with its peers, and the rows
## it hands [NetwReplicationSend].
##
## Every case here asserts a hash, or a row that reaches the lane and what it
## carries, so a shape change that two peers could disagree on and a lane change
## that sends an unchanged column both fail here rather than in a capture. The
## derivation these sets come from, and the node writes they end in, are
## [code]tests/unit/sync/test_script_sync_sets.gd[/code].
##
## [method NetwPropertySet.wire_hash] carries the column order, the lane, the
## declared type and the quantizer, and never the schema name, so two peers
## whose scripts disagree on a shape poison the binding at spawn instead of
## decoding each other's bytes wrong.
class_name TestScriptSyncWire
extends NetwTestSuite


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


func test_the_volatile_row_bails_on_a_missing_field() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var set := _make_set([[&"nonexistent", NetwPropertySet.Lane.VOLATILE]])
	assert_array(NetwPropertySetBinding.new(set, src).volatile_row()).is_empty()


func test_the_retained_row_heals_whole_then_masks_what_moved() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(3, -4)
	src.rotation = 0.5
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE, SchemaCore.ColumnType.VECTOR2],
			[&"rotation", NetwPropertySet.Lane.RETAINED, SchemaCore.ColumnType.F32],
			[&"scale", NetwPropertySet.Lane.RETAINED, SchemaCore.ColumnType.VECTOR2],
		],
	)
	var binding := NetwPropertySetBinding.new(set, src)
	var send := NetwReplicationSend.new()
	send.declare_channel(
		NetwFrameEnvelope.Channel.SYNC_ROW_DELTA,
		&"SYNC_ROW_DELTA",
		true,
	)

	# The retained half is its own row, disjoint from the volatile one, and the
	# offer carries every retained column every pass. What each recipient is
	# owed is the lane's decision, not the binding's.
	assert_array(binding.retained_row()).is_equal([0.5, Vector2(1, 1)])
	assert_array(binding.volatile_row()).is_equal([Vector2(3, -4)])

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var dst_binding := NetwPropertySetBinding.new(set, dst)

	var whole: Array = send.run_deferred([_retained_offer(binding)])["sends"]
	assert_int(whole.size()).is_equal(1)
	assert_dict(dst_binding.apply_retained_row(send, whole[0]["bytes"])).is_not_empty()
	assert_float(dst.rotation).is_equal_approx(0.5, 0.0001)

	# A pass that changed nothing owes nobody anything.
	assert_int((send.run_deferred([_retained_offer(binding)])["sends"] as Array).size()) \
		.is_equal(0)

	src.rotation = 1.5
	var partial: Array = send.run_deferred([_retained_offer(binding)])["sends"]
	assert_int(partial.size()).is_equal(1)
	var header := dst_binding.apply_retained_row(send, partial[0]["bytes"])
	assert_bool(header["whole"]).is_false()
	assert_float(dst.rotation).is_equal_approx(1.5, 0.0001)
	# The column that did not ride is the one the receiver already held.
	assert_that(dst.scale).is_equal(Vector2(1, 1))


func _retained_offer(binding: NetwPropertySetBinding) -> Dictionary:
	return {
		"route": 1,
		"comp": 0,
		"channel": NetwFrameEnvelope.Channel.SYNC_ROW_DELTA,
		"schema": binding.set.retained_schema,
		"values": binding.retained_row(),
		"recipients": PackedInt32Array([2]),
		"tick": -1,
		"ack": -1,
		"reliable": true,
		"priority": 1.0,
	}


