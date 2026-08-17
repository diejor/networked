## Compile parity laws for the flat property set RID surface.
##
## A property set is a binding of a schema, so what is proven here is that the
## flat compile of a script produces the same bytes as the compatibility object
## it replaced, and that a set assembled by hand through the flat verbs binds
## the schema's columns by their one address.
class_name TestPropertySetSurface
extends NetwTestSuite

class ManualBody:
	extends Node2D

	var health := 17


func test_handle_ledger_is_kind_local_and_reversible() -> void:
	var first := NetwHandleLedger.new()
	var second := NetwHandleLedger.new()
	var rid := first.rid_create()

	assert_bool(first.rid_is_valid(rid)).is_true()
	assert_that(first.rid_from_id(1)).is_equal(rid)
	assert_bool(second.rid_is_valid(rid)).is_false()
	assert_bool(first.rid_free(rid)).is_true()
	assert_bool(first.rid_is_valid(rid)).is_false()


func test_from_script_compiles_a_cached_sealed_rid_with_identical_bytes() -> void:
	var mt := MultiplayerTree.new()
	add_child(mt)
	auto_free(mt)
	var node: Node2D = preload(
		"res://tests/support/sync/derived_state_player.gd"
	).new()
	mt.add_child(node)
	auto_free(node)
	var script := node.get_script() as Script
	var old_set := NetwPropertySet.from_script(
		script,
		NetwPropertySet.Record.RECORD_STATE,
		null,
		node,
	) as NetwPropertySet
	var new_set := NetwPropertySet.from_script(
		script,
		NetwPropertySet.Record.RECORD_STATE,
		mt.api,
		node,
	)
	var rid := new_set.rid

	assert_bool(rid.is_valid()).is_true()
	assert_bool(new_set.sealed).is_true()
	assert_bool(new_set != old_set).is_true()
	assert_that(_set_shape(new_set)).is_equal(_set_shape(old_set))
	assert_int(mt.api.property_set_get_wire_hash(rid)).is_equal(
		new_set.wire_hash(),
	)
	assert_array(_row_bytes(new_set, node)).is_equal(_row_bytes(old_set, node))
	var cached := NetwPropertySet.from_script(
		script,
		NetwPropertySet.Record.RECORD_STATE,
		mt.api,
		node,
	)
	assert_that(cached.rid).is_equal(rid)


func test_a_manual_set_binds_the_schema_by_column_address() -> void:
	var mt := MultiplayerTree.new()
	add_child(mt)
	auto_free(mt)
	var node := ManualBody.new()
	mt.add_child(node)
	auto_free(node)
	var entity := NetwEntity.ensure(node)
	mt.api._native_core.liveness_bind_route(7, entity)

	var schema := mt.api.schema_create(&"ManualBody")
	var health := mt.api.schema_add_column(
		schema,
		&"health",
		NetwMultiplayer.ColumnType.COLUMN_I64,
	)
	var stored := mt.api.schema_add_column(
		schema,
		&"stored_only",
		NetwMultiplayer.ColumnType.COLUMN_F64,
	)
	assert_int(mt.api.schema_seal(schema)).is_equal(OK)

	var set := mt.api.property_set_create(
		schema,
		NetwMultiplayer.RecordKind.STATE,
	)
	assert_int(mt.api.property_set_add_column(set, health)).is_equal(0)
	assert_int(mt.api.property_set_seal(set)).is_equal(OK)
	assert_int(mt.api.entity_add_property_set(entity.rid, set, 0)).is_equal(OK)

	assert_that(mt.api.property_set_get_schema(set)).is_equal(schema)
	assert_int(mt.api.entity_get_property(entity.rid, 0, 0)).is_equal(17)
	assert_int(mt.api.schema_get_column_count(schema)).is_equal(2)
	assert_that(mt.api.schema_get_column_key(schema, stored)).is_equal(
		&"stored_only",
	)


## Verify a column the schema declares but no set binds rides no lane, which is
## what keeps a persisted-only value off the wire by construction.
func test_a_column_no_set_binds_rides_no_lane() -> void:
	var mt := MultiplayerTree.new()
	add_child(mt)
	auto_free(mt)
	var schema := mt.api.schema_create(&"PartialBind")
	var synced := mt.api.schema_add_column(
		schema,
		&"synced",
		NetwMultiplayer.ColumnType.COLUMN_F64,
	)
	mt.api.schema_add_column(
		schema,
		&"stored_only",
		NetwMultiplayer.ColumnType.COLUMN_F64,
	)
	mt.api.schema_seal(schema)

	var set := mt.api.property_set_create(
		schema,
		NetwMultiplayer.RecordKind.STATE,
	)
	mt.api.property_set_add_column(set, synced)
	mt.api.property_set_seal(set)

	var record := mt.api._property_set_record(set)
	assert_int(record.columns.size()).is_equal(1)
	assert_array(record.keys()).is_equal([&"synced"] as Array[StringName])
	assert_object(record.member(1)).is_null()


## Verify a stride has no meaning for a node property, so a schema that
## declares one cannot become a property set.
func test_a_strided_schema_cannot_create_a_property_set() -> void:
	var mt := MultiplayerTree.new()
	add_child(mt)
	auto_free(mt)
	var schema := mt.api.schema_create(&"StridedBind")
	mt.api.schema_add_column(
		schema,
		&"cooldown",
		NetwMultiplayer.ColumnType.COLUMN_F32,
		4,
	)
	mt.api.schema_seal(schema)

	assert_bool(
		mt.api.property_set_create(
			schema,
			NetwMultiplayer.RecordKind.STATE,
		).is_valid(),
	).is_false()


# The frame one pass of the volatile lane makes of this set's row on this node.
func _row_bytes(set: NetwPropertySet, node: Node) -> PackedByteArray:
	var send := NetwReplicationSend.new()
	send.declare_channel(NetwFrameEnvelope.Channel.SYNC_ROW, &"SYNC_ROW", false)
	var result: Dictionary = send.run_deferred([{
		"route": 1,
		"comp": 0,
		"channel": NetwFrameEnvelope.Channel.SYNC_ROW,
		"schema": set.volatile_schema,
		"values": NetwPropertySetBinding.new(set, node).volatile_row(),
		"recipients": PackedInt32Array([2]),
		"tick": 12,
		"ack": 9,
		"priority": 1.0,
	}])
	var sends: Array = result["sends"]
	return sends[0]["bytes"] if not sends.is_empty() else PackedByteArray()


func _set_shape(set: NetwPropertySet) -> Array:
	var members: Array = []
	for column: NetwPropertySet.Column in set.columns:
		members.append(
			[
				column.key,
				column.type,
				column.quantizer,
				column.lane,
				column.property_class,
				column.epsilon_override,
				column.teleport_at_override,
				column.carry_channel,
				column.explicit_teleport_only,
				column.explicit_reconcile_only,
				column.converge_stiffness,
			],
		)
	return [
		members,
		set.record,
		set.masked,
		set.window,
		set.audience,
		set.policy,
		set.trigger,
		set.cadence,
		set.stamp,
		set.profile,
		set.channel,
		set.reliable,
	]
