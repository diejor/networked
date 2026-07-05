## Unit tests for [PackedSynchronizer] payload compression.
class_name TestPackedCompression
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


func test_compression_roundtrip() -> void:
	_sync.compression_enabled = true
	_sync.compression_mode = FileAccess.COMPRESSION_ZSTD

	_root.position = Vector2(100, -200)

	# Simulate _read_property (which is what replicates the value over network)
	var serialized: Variant = _sync._read_property(
		_sync.carrier_name(),
		_sync.get_path(),
	)
	assert_bool(serialized is PackedByteArray).is_true()

	# Reset position and simulate _write_property (which receives network value)
	_root.position = Vector2.ZERO
	_sync._write_property(_sync.carrier_name(), _sync.get_path(), serialized)

	# Ensure decompression succeeded and applied the state correctly
	assert_vector(_root.position).is_equal(Vector2(100, -200))


func test_compression_off_does_not_compress() -> void:
	_sync.compression_enabled = false
	_root.position = Vector2(100, -200)

	var uncompressed: Variant = _sync._read_property(
		_sync.carrier_name(),
		_sync.get_path(),
	)

	_sync.compression_enabled = true
	_sync.compression_mode = FileAccess.COMPRESSION_ZSTD
	var compressed: Variant = _sync._read_property(
		_sync.carrier_name(),
		_sync.get_path(),
	)

	# Compressed should be different than uncompressed
	assert_array(compressed).is_not_equal(uncompressed)


func test_compression_max_size_safety_limit() -> void:
	_sync.compression_enabled = true
	_sync.compression_mode = FileAccess.COMPRESSION_ZSTD

	# Set a tiny safety limit (e.g. 2 bytes) which the payload will exceed
	_sync.compression_max_size = 2
	_root.position = Vector2(100, -200)

	var serialized: Variant = _sync._read_property(
		_sync.carrier_name(),
		_sync.get_path(),
	)

	# This should fail to decompress due to safety limit and not mutate position
	_root.position = Vector2.ZERO
	_sync._write_property(_sync.carrier_name(), _sync.get_path(), serialized)

	assert_vector(_root.position).is_equal(Vector2.ZERO)
