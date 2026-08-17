## Laws of the sealed sync engine whose subject is the frame itself.
##
## Every case here asserts bytes, a stream sequence, or a per-peer book, so the
## suite reads the encoder and the progress ledger and never a node's fields.
## The laws whose subject is a node, a property set's shape, or a registration
## live in [code]tests/unit/sync/test_replication_engine_laws.gd[/code].
class_name TestReplicationWireLaws
extends NetwTestSuite


## L2. Decode stages exactly the row that encode emitted.
func test_l2_round_trip() -> void:
	var bytes := NetwSyncKernel.encode_volatile(2, [4, "ready"])
	var staged := NetwSyncKernel.decode_volatile(bytes, [&"score", &"state"])
	assert_object(staged).is_not_null()
	assert_dict(staged.row).is_equal(
		{ &"score": 4, &"state": "ready" },
	)


## P13. Replaying identical value intake produces identical outputs.
## L8. A route's declarations die with the route.
func test_l8_registration_balance() -> void:
	var model := NetwSyncModel.new()
	model.declare(8, NetwSyncModel.Kind.KIND_CONSUMED, &"A", 0, RID(), 0, 4)
	assert_array(model.route_rows(8)).is_not_empty()
	model.clear_route(8)
	assert_array(model.route_rows(8)).is_empty()


## P13. Replaying identical value intake produces identical outputs.
func test_p13_replay_determinism() -> void:
	var left := _replay_capture()
	var right := _replay_capture()
	assert_array(left[&"bytes"]).is_equal(right[&"bytes"])
	assert_array(left[&"operations"]).is_equal(right[&"operations"])


## P14. Declaration and peer scheduling do not change committed results.
func test_p14_schedule_independence() -> void:
	var left := NetwSyncModel.new()
	left.declare(3, NetwSyncModel.Kind.KIND_DERIVED, &"B", 0, RID(), 1, 2)
	left.declare(3, NetwSyncModel.Kind.KIND_CONSUMED, &"A", 0, RID(), 0, 1)
	var right := NetwSyncModel.new()
	right.declare(3, NetwSyncModel.Kind.KIND_CONSUMED, &"A", 0, RID(), 0, 1)
	right.declare(3, NetwSyncModel.Kind.KIND_DERIVED, &"B", 0, RID(), 1, 2)
	assert_array(_model_shape(left)).is_equal(_model_shape(right))


func _single_row(recipients: Array, desired: Dictionary) -> Array[Dictionary]:
	return [
		{
			&"route": 1,
			&"parent_route": 0,
			&"recipients": recipients,
			&"local_desired": desired,
			&"leave": { },
		},
	]


func _model_shape(model: NetwSyncModel) -> Array:
	var out: Array = []
	for row: NetwSyncSetRow in model.route_rows(3):
		out.append([row.ordinal, row.kind, row.key, row.record, row.schema_hash])
	return out


func _replay_capture() -> Dictionary:
	var bytes := NetwSyncKernel.encode_volatile(1, [8])
	var plan := NetwSpawnPlanner.reconcile(
		_single_row([], { 2: true }),
		PackedInt32Array([2]),
	)
	return { &"bytes": bytes, &"operations": plan }
