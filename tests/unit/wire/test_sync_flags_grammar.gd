## Wire-first tests for the [constant NetwFrameEnvelope.Channel.SYNC] frame's
## reserved byte, the one thing a consumed synchronizer's payload carries
## between its ordinal and its values.
##
## These cases hand-build the bytes rather than calling
## [method NetwSyncKernel.encode_volatile], so the layout is pinned
## independently of the encoder that writes it. The two laws are the whole
## grammar: a frame whose reserved byte is [constant NetwSyncKernel.RESERVED]
## reaches the real receive handler and applies, and a frame that wrote anything
## else drops loudly rather than reading the values against a layout this
## version does not have.
class_name TestSyncFlagsGrammar
extends NetwTestSuite

class GrammarProbe:
	extends Node2D

	var synced := 0


var mt: MultiplayerTree
var api: NetwMultiplayer
var native_core: NetwMultiplayerCore
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
	native_core = api._native_core
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
	native_core.liveness_bind_route(ROUTE, entity)
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
	# The reserved byte is the whole header, so the handler reads the values
	# straight after it and applies them.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(NetwSyncKernel.RESERVED)
	NetwCodec.write_values(w, [55], [], [TYPE_INT])

	_drive_sync(w.to_bytes())

	assert_int(root.synced).is_equal(55)
	assert_int(_counter("sync_frames_in")).is_equal(1)


func test_unknown_flag_bit_drops_loudly() -> void:
	# A written reserved byte is a version skew: the frame drops and counts
	# loudly rather than reading whatever followed it as the value count.
	var w := NetwBitBufferWriter.new()
	NetwCodec.put_varint(w, 0)
	w.put_aligned_u8(1)
	NetwCodec.put_varint(w, 1000)
	NetwCodec.write_values(w, [55], [], [TYPE_INT])
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
