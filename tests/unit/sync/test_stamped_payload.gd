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
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
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
