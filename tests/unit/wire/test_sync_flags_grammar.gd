## Wire-first tests for the [constant NetwFrameEnvelope.Channel.SYNC] frame's
## flags grammar, the extension point that folds the stamped, acked, windowed,
## and masked variants onto the one volatile channel.
##
## The unified sync convergence has not landed its encoder yet, so these cases
## hand-build the exact bytes each flag variant frames and assert their layout
## the way the future decoder must read them. A plain (flags [code]0[/code])
## frame is byte-identical to the landed consumed frame and still drives the
## real receive handler, and a frame carrying a flag bit this version cannot
## read drops loudly rather than misreading the framed payload.
class_name TestSyncFlagsGrammar
extends NetwTestSuite

class GrammarProbe:
	extends Node2D

	var synced := 0


var mt: MultiplayerTree
var api: NetwMultiplayer
var liveness: LivenessShell
var root: GrammarProbe
var sync: MultiplayerSynchronizer
var entity: NetwEntity
const ROUTE := 7


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "SyncFlagsTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	liveness = api._liveness
	root = GrammarProbe.new()
	root.name = "GrammarProbe"
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
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root

	entity = NetwEntity.ensure(root)
	mt.add_child(root)
	auto_free(root)
	liveness.bind_route(ROUTE, entity)
	api.object_configuration_add(root, sync)


func _drive_sync(payload: PackedByteArray) -> void:
	var frame := NetwFrameEnvelope.pack(
		ROUTE,
		0,
		NetwFrameEnvelope.Channel.SYNC,
		payload,
	)
	api._drive_carrier(1, frame, true)


func _counter(key: String) -> int:
	return int(api.stats_snapshot().get(key, 0))


func test_plain_frame_flags_zero_applies() -> void:
	# Flags 0 is the plain consumed frame, byte-identical to the landed wire, so
	# the real handler still reads it and applies the value.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(0)
	NetwScriptModel.write_values(w, [55], [], [TYPE_INT])

	_drive_sync(w.to_bytes())

	assert_int(root.synced).is_equal(55)
	assert_int(_counter("sync_frames_in")).is_equal(1)


func test_unknown_flag_bit_drops_loudly() -> void:
	# The consumed decoder implements only the plain grammar, so a stamped bit is
	# a version skew: the frame drops and counts loudly rather than reading the
	# tick varint as the value count.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(NetwFrameEnvelope.SYNC_FLAG_STAMPED)
	NetwCodec.put_varint(w, 1000)
	NetwScriptModel.write_values(w, [55], [], [TYPE_INT])
	var bytes := w.to_bytes()

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			_drive_sync(bytes)
	).is_push_warning(
		"NetwSyncCompat: consumed synchronizer 'Sync' SYNC flags 1 unimplemented, "
		+ "frame dropped. Peers must run the same Networked version.",
	)

	assert_int(root.synced).is_equal(0)
	assert_int(_counter("drops_sync_unknown_flag")).is_equal(1)
	assert_int(_counter("sync_frames_in")).is_equal(0)


func test_stamped_frame_carries_tick_after_flags() -> void:
	# The stamped variant frames the authoring tick as a varint immediately after
	# the flags byte, ahead of the positional values.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 2)
	w.put_aligned_u8(NetwFrameEnvelope.SYNC_FLAG_STAMPED)
	NetwCodec.put_varint(w, 4096)
	NetwScriptModel.write_values(w, [12], [], [TYPE_INT])

	var r := NetwBitBufferReader.create(w.to_bytes())
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(2)
	assert_int(r.get_aligned_u8()).is_equal(NetwFrameEnvelope.SYNC_FLAG_STAMPED)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(4096)
	assert_array(NetwScriptModel.read_values(r, [], [])).is_equal([12])


func test_acked_frame_encodes_ack_as_value_plus_one() -> void:
	# The reconciliation ack rides as the acked value plus one so the
	# no-input-consumed sentinel -1 crosses as zero, keeping the varint unsigned.
	for pair: Array in [[-1, 0], [0, 1], [41, 42]]:
		var ack_value: int = pair[0]
		var on_wire: int = pair[1]
		var w := NetwBitBufferWriter.new()
		NetwCodec.put_varint(w, 0)
		w.put_aligned_u8(
			NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_ACKED,
		)
		NetwCodec.put_varint(w, 7) # tick
		NetwCodec.put_varint(w, ack_value + 1)

		var r := NetwBitBufferReader.create(w.to_bytes())
		NetwCodec.get_safe_varint(r) # ordinal
		r.get_aligned_u8() # flags
		NetwCodec.get_safe_varint(r) # tick
		var raw := NetwCodec.get_safe_varint(r)
		assert_int(raw).is_equal(on_wire)
		assert_int(raw - 1).is_equal(ack_value)


func test_windowed_frame_carries_count_then_age_rows() -> void:
	# The windowed variant frames a sample count then one row per sample, each
	# prefixed by its age in ticks, newest first at age zero.
	var samples := [[0, 30], [1, 20], [2, 10]] # [age, value], newest first
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(
		NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_WINDOWED,
	)
	NetwCodec.put_varint(w, 9) # tick
	NetwCodec.put_varint(w, samples.size())
	for s: Array in samples:
		NetwCodec.put_varint(w, s[0])
		NetwScriptModel.write_values(w, [s[1]], [], [TYPE_INT])

	var r := NetwBitBufferReader.create(w.to_bytes())
	NetwCodec.get_safe_varint(r) # ordinal
	r.get_aligned_u8() # flags
	NetwCodec.get_safe_varint(r) # tick
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(3)
	for expected: Array in samples:
		assert_int(NetwCodec.get_safe_varint(r)).is_equal(expected[0])
		assert_array(NetwScriptModel.read_values(r, [], [])).is_equal([expected[1]])


func test_flag_bits_occupy_distinct_positions() -> void:
	# The five live grammar bits never collide when a frame combines them.
	assert_int(NetwFrameEnvelope.SYNC_FLAG_STAMPED).is_equal(1)
	assert_int(NetwFrameEnvelope.SYNC_FLAG_ACKED).is_equal(2)
	assert_int(NetwFrameEnvelope.SYNC_FLAG_WINDOWED).is_equal(4)
	assert_int(NetwFrameEnvelope.SYNC_FLAG_TAPED).is_equal(8)
	assert_int(NetwFrameEnvelope.SYNC_FLAG_MASKED).is_equal(16)
	var combined := (
			NetwFrameEnvelope.SYNC_FLAG_STAMPED
			| NetwFrameEnvelope.SYNC_FLAG_ACKED
			| NetwFrameEnvelope.SYNC_FLAG_WINDOWED
			| NetwFrameEnvelope.SYNC_FLAG_TAPED
			| NetwFrameEnvelope.SYNC_FLAG_MASKED
	)
	assert_int(combined).is_equal(31)
