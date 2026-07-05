## Unit tests for [StampedSynchronizer] payload capture and restore.
##
## [method StampedSynchronizer.snapshot_payload] reads every payload virtual prop
## except the stamps; [method StampedSynchronizer.apply_payload] writes them back
## onto the live node, bypassing [member StampedSynchronizer.write_through] so a
## reconciliation restore always lands. No networking, just the proxy hooks.
class_name TestStampedPayload
extends NetwTestSuite

var _root: Node2D
var _sync: StateSynchronizer


func before_test() -> void:
	_root = Node2D.new()
	add_child(_root)
	auto_free(_root)

	_sync = StateSynchronizer.new()
	_sync.name = "StateSync"
	_sync.register_property(
		&"position",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	_root.add_child(_sync)
	_sync.owner = _root
	_sync.root_path = _sync.get_path_to(_root)
	await get_tree().process_frame


func test_snapshot_excludes_stamps_and_captures_payload() -> void:
	_root.position = Vector2(10, -20)
	var snap := _sync.snapshot_payload()

	assert_vector(snap.get(&"position")).is_equal(Vector2(10, -20))
	# The tick and ack stamps are not payload.
	assert_bool(snap.has(StampedSynchronizer.TICK)).is_false()
	assert_bool(snap.has(StateSynchronizer.ACK)).is_false()


func test_apply_payload_writes_through_even_when_write_through_is_false() -> void:
	# The owning client keeps write_through false so the network never snaps the
	# predicted body, yet a correction restore still must land on it.
	_sync.write_through = false
	_root.position = Vector2(10, -20)
	var snap := _sync.snapshot_payload()

	_root.position = Vector2.ZERO
	_sync.apply_payload(snap)
	assert_vector(_root.position).is_equal(Vector2(10, -20))


func test_apply_payload_ignores_unregistered_keys() -> void:
	_sync.apply_payload({ &"position": Vector2(3, 4), &"unknown": 99 })
	assert_vector(_root.position).is_equal(Vector2(3, 4))


func test_rpc_state_change_gate_ignores_tick_only_change() -> void:
	var pair := await _rpc_state_sync(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	var root := pair.root as Node2D
	var sync := pair.sync as StateSynchronizer

	root.position = Vector2(1, 2)
	sync.authored_tick = 10
	assert_int(sync._rpc_outgoing_bytes(10).size()).is_greater(0)
	sync._rpc_last_sent_tick = 10

	sync.authored_tick = 11
	assert_int(sync._rpc_outgoing_bytes(11).size()).is_equal(0)

	root.position = Vector2(3, 4)
	assert_int(sync._rpc_outgoing_bytes(12).size()).is_greater(0)


func test_rpc_state_heartbeat_forces_unchanged_payload() -> void:
	var pair := await _rpc_state_sync(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	var sync := pair.sync as StateSynchronizer
	sync.heartbeat_ticks = 2
	sync.authored_tick = 10
	assert_int(sync._rpc_outgoing_bytes(10).size()).is_greater(0)
	sync._rpc_last_sent_tick = 10

	sync.authored_tick = 11
	assert_int(sync._rpc_outgoing_bytes(11).size()).is_equal(0)

	sync.authored_tick = 12
	assert_int(sync._rpc_outgoing_bytes(12).size()).is_greater(0)


func test_rpc_retained_change_marks_reliable() -> void:
	var pair := await _rpc_state_sync(SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	var root := pair.root as Node2D
	var sync := pair.sync as StateSynchronizer

	root.position = Vector2(5, 6)
	sync.authored_tick = 10
	assert_int(sync._rpc_outgoing_bytes(10).size()).is_greater(0)
	assert_bool(sync._rpc_force_reliable).is_true()


func test_rpc_state_drops_stale_carrier() -> void:
	var pair := await _rpc_state_sync(SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	var root := pair.root as Node2D
	var sync := pair.sync as StateSynchronizer

	root.position = Vector2(2, 0)
	sync.authored_tick = 2
	var newer := sync._pack_wire_bytes(sync.encode_carrier())
	root.position = Vector2(1, 0)
	sync.authored_tick = 1
	var older := sync._pack_wire_bytes(sync.encode_carrier())

	root.position = Vector2.ZERO
	sync._apply_rpc_carrier(newer)
	assert_vector(root.position).is_equal(Vector2(2, 0))
	sync._apply_rpc_carrier(older)
	assert_vector(root.position).is_equal(Vector2(2, 0))


func _rpc_state_sync(mode: SceneReplicationConfig.ReplicationMode) -> Dictionary:
	var root := Node2D.new()
	add_child(root)
	auto_free(root)

	var sync := StateSynchronizer.new()
	sync.name = "RpcStateSync"
	sync.transport = PackedSynchronizer.Transport.RPC
	sync.register_property(&"position", NodePath(".:position"), mode)
	root.add_child(sync)
	sync.owner = root
	sync.root_path = sync.get_path_to(root)
	await get_tree().process_frame
	return { &"root": root, &"sync": sync }
