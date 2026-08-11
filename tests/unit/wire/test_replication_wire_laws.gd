## Laws of the sealed sync engine whose subject is the frame itself.
##
## Every case here asserts bytes, a stream sequence, or a per-peer book, so the
## suite reads the encoder and the progress ledger and never a node's fields.
## The laws whose subject is a node, a property set's shape, or a registration
## live in [code]tests/unit/sync/test_replication_engine_laws.gd[/code].
class_name TestReplicationWireLaws
extends NetwTestSuite


## L1. The extracted encoder is byte-identical to the frozen codec.
func test_l1_byte_parity() -> void:
	var frame := {
		&"ordinal": 3,
		&"flags": NetwFrameEnvelope.SYNC_FLAG_STAMPED,
		&"values": [12, Vector2(2.0, 4.0)],
		&"quantizers": [],
		&"types": [TYPE_INT, TYPE_VECTOR2],
		&"tick": 77,
		&"ack": -1,
	}
	var expected := NetwFrameEnvelope.encode_sync_frame(frame)
	var actual := NetwSyncKernel.encode_volatile(
		3,
		NetwFrameEnvelope.SYNC_FLAG_STAMPED,
		frame[&"values"],
		[],
		frame[&"types"],
		77,
		-1,
	)
	assert_array(actual).is_equal(expected)


## L2. Decode stages exactly the row that encode emitted.
func test_l2_round_trip() -> void:
	var bytes := NetwSyncKernel.encode_volatile(
		2,
		0,
		[4, "ready"],
		[],
		[TYPE_INT, TYPE_STRING],
		-1,
		-1,
	)
	var staged := NetwSyncKernel.decode_volatile(
		bytes,
		[&"score", &"state"],
		[],
		[TYPE_INT, TYPE_STRING],
	)
	assert_object(staged).is_not_null()
	assert_dict(staged.row).is_equal(
		{ &"score": 4, &"state": "ready" },
	)


## L3. Stream freshness advances monotonically across the u16 ring.
func test_l3_freshness_monotonicity() -> void:
	var progress := NetwSyncProgress.new()
	assert_bool(progress.accept_unreliable(2, 7, 19, 65535)).is_true()
	assert_bool(progress.accept_unreliable(2, 7, 19, 0)).is_true()
	assert_bool(progress.accept_unreliable(2, 7, 19, 65535)).is_false()
	assert_bool(progress.accept_unreliable(2, 7, 20, 1)).is_true()


## L4. Masked rows reconstruct against the last committed full row.
func test_l4_masked_convergence() -> void:
	var full := NetwSyncKernel.encode_volatile(
		0,
		NetwFrameEnvelope.SYNC_FLAG_MASKED,
		[1, 2],
		[null, null],
		[TYPE_INT, TYPE_INT],
		1,
		-1,
		3,
	)
	var first := NetwSyncKernel.decode_volatile(
		full,
		[&"x", &"y"],
		[null, null],
		[TYPE_INT, TYPE_INT],
	)
	var partial := NetwSyncKernel.encode_volatile(
		0,
		NetwFrameEnvelope.SYNC_FLAG_MASKED,
		[9],
		[null],
		[TYPE_INT],
		2,
		-1,
		1,
	)
	var second := NetwSyncKernel.decode_volatile(
		partial,
		[&"x", &"y"],
		[null, null],
		[TYPE_INT, TYPE_INT],
		first.row,
	)
	assert_dict(second.row).is_equal({ &"x": 9, &"y": 2 })


## L8. Route, peer, and session clears leave no engine residue.
func test_l8_registration_balance() -> void:
	var model := NetwSyncModel.new()
	model.declare(8, NetwSyncModel.Kind.CONSUMED, &"A", 0, RID(), 0, 4)
	var progress := NetwSyncProgress.new()
	progress.accept_unreliable(2, 8, 19, 1)
	progress.stage_masked(2, 8, &"A", 0, { &"x": 1 })
	model.clear_route(8)
	progress.clear_route(8)
	assert_array(model.route_rows(8)).is_empty()
	assert_dict(progress.stats()).is_equal(
		{ &"streams": 0, &"pending_masked": 0 },
	)


## L10. Transport progress and reconciliation ticks are separate domains.
func test_l10_ack_duality() -> void:
	var progress := NetwSyncProgress.new()
	progress.stage_masked(2, 9, &"state", 0, { &"x": 7 })
	assert_bool(progress.accept_unreliable(2, 9, 19, 10)).is_true()
	assert_int(progress.take_masked(2).size()).is_equal(1)
	assert_bool(progress.accept_unreliable(2, 9, 19, 10)).is_false()


## P13. Replaying identical value intake produces identical outputs.
func test_p13_replay_determinism() -> void:
	var left := _replay_capture()
	var right := _replay_capture()
	assert_array(left[&"bytes"]).is_equal(right[&"bytes"])
	assert_array(left[&"operations"]).is_equal(right[&"operations"])


## P14. Declaration and peer scheduling do not change committed results.
func test_p14_schedule_independence() -> void:
	var left := NetwSyncModel.new()
	left.declare(3, NetwSyncModel.Kind.DERIVED, &"B", 0, RID(), 1, 2)
	left.declare(3, NetwSyncModel.Kind.CONSUMED, &"A", 0, RID(), 0, 1)
	var right := NetwSyncModel.new()
	right.declare(3, NetwSyncModel.Kind.CONSUMED, &"A", 0, RID(), 0, 1)
	right.declare(3, NetwSyncModel.Kind.DERIVED, &"B", 0, RID(), 1, 2)
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
	for row: NetwSyncModel.SetRow in model.route_rows(3):
		out.append([row.ordinal, row.kind, row.key, row.record, row.schema_hash])
	return out


func _replay_capture() -> Dictionary:
	var bytes := NetwSyncKernel.encode_volatile(
		1,
		0,
		[8],
		[],
		[TYPE_INT],
		-1,
		-1,
	)
	var plan := NetwSpawnReconciler.reconcile(
		_single_row([], { 2: true }),
		PackedInt32Array([2]),
	)
	return { &"bytes": bytes, &"operations": plan.operations }
