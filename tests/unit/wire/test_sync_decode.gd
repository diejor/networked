## Wire-first tests for the consumed synchronizer frames: the
## [constant NetwFrameEnvelope.Channel.SYNC] payload
## [code][ordinal | flags | values][/code] and the reliable
## [constant NetwFrameEnvelope.Channel.SYNC_DELTA] payload
## [code][ordinal | mask | values][/code].
##
## Every case hand-builds the payload bytes and drives them through the real
## receive handlers, so the byte layout these tests pin is the wire contract a
## conforming decoder must read. Malformed frames, an unknown ordinal, a sender
## that is not the authority, and a schema fingerprint that disagrees with the
## spawn descriptor must all drop or poison loudly instead of misreading state.
class_name TestSyncDecode
extends NetwTestSuite

class DecodeProbe:
	extends Node2D

	var synced := 0
	var watched := 0


var mt: MultiplayerTree
var api: NetwMultiplayer
var native_core: NetwMultiplayerCore
var root: DecodeProbe
var sync: MultiplayerSynchronizer
var entity: NetwEntity
const ROUTE := 7


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "SyncDecodeTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	native_core = api._native_core
	root = DecodeProbe.new()
	root.name = "DecodeProbe"
	sync = MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var sync_prop := NodePath(".:synced")
	cfg.add_property(sync_prop)
	cfg.property_set_replication_mode(
		sync_prop,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	var watch_prop := NodePath(".:watched")
	cfg.add_property(watch_prop)
	cfg.property_set_replication_mode(
		watch_prop,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root

	entity = NetwEntity.ensure(root)
	mt.add_child(root)
	auto_free(root)
	native_core.liveness_bind_route(ROUTE, entity)
	api.object_configuration_add(root, sync)


func _drive_sync(channel: NetwFrameEnvelope.Channel, payload: PackedByteArray) \
-> Error:
	return _drive_sync_from(1, channel, payload)


func _drive_sync_from(
		sender: int,
		channel: NetwFrameEnvelope.Channel,
		payload: PackedByteArray,
) -> Error:
	var frame := NetwFrameEnvelope.pack(ROUTE, 0, channel, payload)
	return api._drive_carrier(sender, frame, true)


func _counter(key: String) -> int:
	return int(api.stats_snapshot().get(key, 0))


func _sync_payload(ordinal: int, flags: int, values: Array) -> PackedByteArray:
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, ordinal)
	w.put_aligned_u8(flags)
	var types: Array = []
	for v in values:
		types.append(typeof(v))
	NetwCodec.write_values(w, values, [], types)
	return w.to_bytes()


func _delta_payload(ordinal: int, mask: int, values: Array) -> PackedByteArray:
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, ordinal)
	NetwCodec.put_varint(w, mask)
	var types: Array = []
	for v in values:
		types.append(typeof(v))
	NetwCodec.write_values(w, values, [], types)
	return w.to_bytes()


func test_sync_frame_applies_and_fires_signal() -> void:
	var fired := [0]
	sync.synchronized.connect(func() -> void: fired[0] += 1)

	_drive_sync(NetwFrameEnvelope.Channel.SYNC, _sync_payload(0, 0, [55]))

	assert_int(root.synced).is_equal(55)
	assert_int(_counter("sync_frames_in")).is_equal(1)
	assert_int(fired[0]).is_equal(1)


func test_delta_frame_applies_masked_field() -> void:
	var fired := [0]
	sync.delta_synchronized.connect(func() -> void: fired[0] += 1)

	_drive_sync(
		NetwFrameEnvelope.Channel.SYNC_DELTA,
		_delta_payload(0, 1, [88]),
	)

	assert_int(root.watched).is_equal(88)
	assert_int(_counter("delta_frames_in")).is_equal(1)
	assert_int(fired[0]).is_equal(1)


func test_unknown_ordinal_drops() -> void:
	_drive_sync(
		NetwFrameEnvelope.Channel.SYNC,
		_sync_payload(5, 0, [1]),
	)

	assert_int(root.synced).is_equal(0)
	assert_int(_counter("drops_sync_no_set")).is_equal(1)
	assert_int(_counter("sync_frames_in")).is_equal(0)


func test_non_authority_sender_drops() -> void:
	# The synchronizer's authority is peer 1, so a frame stamped by peer 2 is
	# never allowed to author the stream.
	_drive_sync_from(
		2,
		NetwFrameEnvelope.Channel.SYNC,
		\
		_sync_payload(0, 0, [99]),
	)

	assert_int(root.synced).is_equal(0)
	assert_int(_counter("drops_sync_bad_sender")).is_equal(1)


func test_schema_hash_mismatch_poisons() -> void:
	# A spawn descriptor that disagrees with the receiver's translated set means
	# the two peers hold different configs, so every frame is unreadable and the
	# binding poisons loudly rather than misreading state.
	var direct := NetwSyncCompat.new(api)
	direct.consume(root, sync)
	direct.note_schema(ROUTE, { 0: 0x1234 })

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			direct.handle_sync(entity, _sync_payload(0, 0, [7]), 1)
	).is_push_error(GdUnitArgumentMatchers.any())

	assert_int(root.synced).is_equal(0)
	assert_int(direct.counters()[&"drops_sync_poisoned"]).is_equal(1)

	# A poisoned binding stays poisoned: later frames keep dropping, quietly now.
	direct.handle_sync(entity, _sync_payload(0, 0, [8]), 1)
	assert_int(root.synced).is_equal(0)
	assert_int(direct.counters()[&"drops_sync_poisoned"]).is_equal(2)
	direct.dispose()


func test_sync_row_size_mismatch_poisons() -> void:
	# The set has one always-replicated field, so a two-value row cannot be the
	# same schema and must poison rather than apply a partial row.
	var expected := (
			"NetwSyncCompat: consumed synchronizer 'Sync' (route 7) poisoned: "
			+ "SYNC row size mismatch. "
			+ "Its replication config must be identical on every peer."
	)

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			_drive_sync(
				NetwFrameEnvelope.Channel.SYNC,
				_sync_payload(0, 0, [1, 2]),
			)
	).is_push_error(expected)

	assert_int(root.synced).is_equal(0)
	assert_int(_counter("drops_sync_poisoned")).is_equal(1)


func test_delta_mask_value_disagreement_poisons() -> void:
	# The mask selects one field but the payload carries no value, so the frame
	# is incoherent and poisons the binding.
	var expected := (
			"NetwSyncCompat: consumed synchronizer 'Sync' (route 7) poisoned: "
			+ "SYNC_DELTA mask and value count disagree. "
			+ "Its replication config must be identical on every peer."
	)

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			_drive_sync(
				NetwFrameEnvelope.Channel.SYNC_DELTA,
				_delta_payload(0, 1, []),
			)
	).is_push_error(expected)

	assert_int(root.watched).is_equal(0)
	assert_int(_counter("drops_sync_poisoned")).is_equal(1)


func test_sync_frame_routes_through_carrier_dispatch() -> void:
	# A full carrier datagram proves channel 19 reaches the sync handler through
	# the envelope, not only the direct handler call.
	var frame := NetwFrameEnvelope.pack(
		ROUTE,
		0,
		NetwFrameEnvelope.Channel.SYNC,
		_sync_payload(0, 0, [21]),
	)
	_deliver_unreliable(5, [frame])

	assert_int(root.synced).is_equal(21)
	assert_int(_counter("sync_frames_in")).is_equal(1)


func test_stale_sync_datagram_drops() -> void:
	# Consumed sync rides the per datagram sequence gate, so a reordered datagram
	# with no fresher state is discarded before it reaches the handler.
	var frame_fresh := NetwFrameEnvelope.pack(
		ROUTE,
		0,
		NetwFrameEnvelope.Channel.SYNC,
		_sync_payload(0, 0, [21]),
	)
	var frame_stale := NetwFrameEnvelope.pack(
		ROUTE,
		0,
		NetwFrameEnvelope.Channel.SYNC,
		_sync_payload(0, 0, [17]),
	)
	_deliver_unreliable(5, [frame_fresh])
	_deliver_unreliable(3, [frame_stale])

	assert_int(root.synced).is_equal(21)
	assert_int(_counter("sync_drops_stale")).is_greater(0)


func test_over_64_watch_properties_poisons_at_consumption() -> void:
	# The delta mask is a single u64, so a config declaring more than 64 on-change
	# properties is unrepresentable and must fail loudly at consumption rather
	# than silently corrupt its bitmask the way the native replicator does.
	var over := DecodeProbe.new()
	over.name = "OverWatch"
	var over_sync := MultiplayerSynchronizer.new()
	over_sync.name = "OverSync"
	over_sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	for i in 65:
		var path := NodePath(".:w%d" % i)
		cfg.add_property(path)
		cfg.property_set_replication_mode(
			path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		)
	over_sync.replication_config = cfg
	over.add_child(over_sync)
	over_sync.owner = over
	auto_free(over)

	var expected := (
			"NetwSyncCompat: consumed synchronizer 'OverSync' (route 0) poisoned: "
			+ "watches 65 properties, above the 64-bit delta mask limit. "
			+ "Its replication config must be identical on every peer."
	)

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			api.object_configuration_add(over, over_sync)
	).is_push_error(expected)


func _deliver_unreliable(seq: int, frames: Array) -> void:
	var packet := PackedByteArray()
	for frame: PackedByteArray in frames:
		packet.append_array(frame)
	api._drive_carrier(1, packet, false, seq)
