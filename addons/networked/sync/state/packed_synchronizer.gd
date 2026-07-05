@tool
## [ProxySynchronizer] that sends its payload as one [NetwCodec] carrier.
##
## The carrier is the wire contract. Payload properties remain registered so
## [method snapshot_payload] and [method apply_payload] can read and write live
## nodes, but the standalone payload rows are suppressed on the wire.
##
## [codeblock]
## var sync := PackedSynchronizer.new()
## sync.register_property(&"position", NodePath(".:position")).quantize(pos_codec)
## sync.register_property(&"velocity", NodePath(".:velocity")).quantize(vel_codec)
## sync.transport = PackedSynchronizer.Transport.RPC
## [/codeblock]
##
## Replication modes on payload rows are delivery intent for this hierarchy:
## [br]- [constant SceneReplicationConfig.REPLICATION_MODE_ALWAYS]: volatile.
## Freshest wins. Loss is healed by a later carrier.
## [br]- [constant SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE]: retained.
## Eventual delivery is required. Missing means unchanged.
## [br]- [constant SceneReplicationConfig.REPLICATION_MODE_NEVER]: suppressed.
## Readable by [method snapshot_payload], but never sent alone.
## [br][br]
## [constant SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE] no longer means
## per-property diffing because the carrier is atomic.
## [constant SceneReplicationConfig.REPLICATION_MODE_ALWAYS] no longer means
## timer cadence when [constant Transport.RPC] is selected. It means
## newest-wins delivery at the cadence chosen by this synchronizer.
class_name PackedSynchronizer
extends ProxySynchronizer

## Virtual name of the bare bundled-payload carrier.
const PACKED := &"__packed"

## Backing transport for the virtual-property surface.
enum Transport {
	## Godot [SceneMultiplayer] replication through a carrier property.
	STOCK,
	## [PackedSynchronizer] RPC carrier sends gated by local policy.
	RPC,
}

## Selected carrier transport.
@export var transport: Transport = Transport.STOCK

## Maximum ticks between forced volatile state sends on [constant Transport.RPC].
##
## A value of [code]0[/code] derives one second from [MultiplayerClock.tickrate].
@export_range(0, 600, 1) var heartbeat_ticks: int = 0

@export_group("Compression", "compression_")

## Compress the bundled payload byte array.
##
## The compressed payload reduces bandwidth usage for large state snapshots.
## It defaults to false because compression overhead makes small payloads larger.
## Set [member compression_enabled] on [PackedSynchronizer] to toggle it.
@export var compression_enabled: bool = false

## Compression algorithm used for the payload.
##
## The algorithm trades CPU cycles for smaller network payloads.
## The value uses [enum FileAccess.CompressionMode] to configure the compressor.
@export var compression_mode: FileAccess.CompressionMode = FileAccess.COMPRESSION_ZSTD

## Maximum allowed decompressed size of a received payload.
##
## This acts as a safety limit to prevent memory-exhaustion exploits.
## The receiving side discards decompressed packets exceeding [member compression_max_size].
@export var compression_max_size: int = 65536

## Maps a payload virtual name to the [NetwQuantize] that bit-packs it on the
## wire.
##
## A property absent from this map byte-aligns and writes a self-describing raw
## value, so quantization is purely additive over the stock path. The inspector
## exposes one [code]codec/<prop>[/code] slot per payload property and stores the
## choice here. The same [NetwQuantize] may back several properties by reference.
## [codeblock]
## sync.set_property_codec(&"position", NetwQuantizeFixed.new())
## [/codeblock]
@export var property_codecs: Dictionary[StringName, NetwQuantize] = { }

var _rpc_clock: MultiplayerClock = null
var _rpc_last_sent_tick: int = -1
var _rpc_last_received_tick: int = -1
var _rpc_last_reliable_core := PackedByteArray()
var _rpc_last_core := PackedByteArray()
var _rpc_force_reliable: bool = false


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	configure()
	finalize()
	_refresh_rpc_driver()


## Override to set authority and register stamps and payload before finalize.
func configure() -> void:
	if carrier_enabled() and transport == Transport.STOCK:
		register_stamp(carrier_name(), SceneReplicationConfig.REPLICATION_MODE_ALWAYS)


## Suppresses the bundled payload props to
## [constant SceneReplicationConfig.REPLICATION_MODE_NEVER] after
## [method ProxySynchronizer.finalize] builds the config.
##
## The carrier replicates the whole payload, so the standalone props are
## redundant. They stay registered so [method snapshot_payload] and the codec can
## still read them.
func finalize() -> void:
	super.finalize()
	if not carrier_enabled() or not replication_config:
		return
	_warn_retained_stock_props()
	var payload := _payload_keys()
	for path: NodePath in replication_config.get_properties():
		var sub := path.get_subname_count()
		if sub == 0:
			continue
		var vname := StringName(path.get_subname(sub - 1))
		if vname in payload:
			replication_config.property_set_replication_mode(
				path,
				SceneReplicationConfig.REPLICATION_MODE_NEVER,
			)


## Registers [param vname] as a stamp on the stream implied by [param mode].
##
## ON_CHANGE rides the reliable, ordered delta (watched). ALWAYS rides the
## volatile newest-wins sync (unwatched). Never split a stamp from the payload
## it tags across the two streams.
func register_stamp(
		vname: StringName,
		mode: SceneReplicationConfig.ReplicationMode,
) -> void:
	register_property(
		vname,
		NodePath(""),
		mode,
		false,
		mode == SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)


func set_property_codec(vname: StringName, quantizer: NetwQuantize) -> void:
	if quantizer == null:
		property_codecs.erase(vname)
	else:
		property_codecs[vname] = quantizer


## Reads every payload virtual property into a [code]{vname: value}[/code]
## [Dictionary].
##
## The stamps ([method _ordered_virtual_names], including the carrier and any
## subclass tick or ack) are excluded, so the result is exactly the snapshot the
## carrier encodes and the receiving peer applies. Keys are stable across peers
## because they come from the same registered config, which is what lets a
## predicting client and the server compare state.
## [codeblock]
## var input := entity.input.snapshot_payload()   # {motion: ..., bombing: ...}
## timeline.record_input(tick, input)
## [/codeblock]
func snapshot_payload() -> Dictionary:
	var stamps := _ordered_virtual_names()
	var out: Dictionary = { }
	for vname: StringName in get_virtual_properties():
		if vname in stamps:
			continue
		out[vname] = _read_property(vname, get_real_path(vname))
	return out


## Override to gate the bundled carrier. The base always enables it.
func carrier_enabled() -> bool:
	return true


## Override to name the bundled carrier virtual property. The base returns
## [constant PACKED].
func carrier_name() -> StringName:
	return PACKED


## Encodes the carrier blob. The base packs the bare payload with no header.
##
## Subclasses override to prepend their framing (a tick stamp, an ack, or a
## redundancy window) ahead of the same [method NetwCodec.encode_payload] core.
func encode_carrier() -> PackedByteArray:
	return encode_payload_core()


## Decodes the carrier blob and writes each payload value onto the live node. The
## base reads the bare payload written by [method encode_carrier].
func decode_carrier(value: Variant) -> void:
	if not (value is PackedByteArray):
		return
	var payload := decode_payload_core(value)
	for k: StringName in payload:
		super._write_property(k, get_real_path(k), payload[k])


func _ordered_virtual_names() -> Array[StringName]:
	if carrier_enabled() and transport == Transport.STOCK:
		return [carrier_name()]
	return []


func _read_property(name: StringName, path: NodePath) -> Variant:
	if carrier_enabled() and name == carrier_name():
		return _pack_wire_bytes(encode_carrier())
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if carrier_enabled() and name == carrier_name():
		if not (value is PackedByteArray):
			return
		var bytes := _unpack_wire_bytes(value)
		if bytes.is_empty() and not (value as PackedByteArray).is_empty():
			return
		decode_carrier(bytes)
		return
	super._write_property(name, path, value)


## Encodes the payload core without tick or ack framing.
func encode_payload_core(keys: Array[StringName] = []) -> PackedByteArray:
	if keys.is_empty():
		keys = _payload_keys()
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.encode_payload(w, snapshot_payload(), keys, _payload_quantizers(keys))
	return w.to_bytes()


## Decodes a payload core without tick or ack framing.
func decode_payload_core(value: PackedByteArray, keys: Array[StringName] = []) -> Dictionary:
	if keys.is_empty():
		keys = _payload_keys()
	var r := NetwBitBuffer.Reader.new(value)
	return NetwCodec.decode_payload(
		r,
		keys,
		_payload_quantizers(keys),
		_payload_types(keys),
	)


func _enter_tree() -> void:
	_refresh_rpc_driver()


func _exit_tree() -> void:
	_refresh_rpc_driver()


func set_multiplayer_authority(id: int, recursive: bool = true) -> void:
	super.set_multiplayer_authority(id, recursive)
	_refresh_rpc_driver()


func _receive_relay_payload(payload: PackedByteArray, sender: int) -> void:
	if sender != get_multiplayer_authority():
		return
	_apply_rpc_carrier(payload)


func _apply_rpc_carrier(bytes: PackedByteArray) -> void:
	var decoded := _unpack_wire_bytes(bytes)
	if decoded.is_empty() and not bytes.is_empty():
		return
	decode_carrier(decoded)
	_on_rpc_carrier_flushed()


func _on_rpc_carrier_flushed() -> void:
	pass


func _refresh_rpc_driver() -> void:
	if Engine.is_editor_hint():
		return
	var relay := RelayService.for_node(self)
	if not relay:
		return

	if is_inside_tree() and transport == Transport.RPC and is_multiplayer_authority():
		relay.register_sender(self)
	else:
		relay.unregister_sender(self)


func _relay_outgoing_bytes(tick: int) -> Array:
	var bytes := _rpc_outgoing_bytes(tick)
	_rpc_last_sent_tick = tick
	return [bytes, _rpc_force_reliable]


func _rpc_outgoing_bytes(_tick: int) -> PackedByteArray:
	_rpc_force_reliable = _retained_changed()
	return _pack_wire_bytes(encode_carrier())


## Override to name the [enum RelayService.Command] this synchronizer's
## carrier rides. [StateSynchronizer] and [InputSynchronizer] each claim their
## own channel.
func relay_channel() -> int:
	assert(false, "override relay_channel")
	return -1


## Override to narrow who receives this synchronizer's relay sends. The base
## fans out to [method LivenessService.live_peers] on the server and to the
## server on a client. [InputSynchronizer] narrows this to
## [member InputSynchronizer.audience].
func relay_recipients(liveness: LivenessService, entity: NetwEntity) -> Array[int]:
	if multiplayer and multiplayer.is_server():
		return liveness.live_peers(entity)
	return [1]


func _retained_changed() -> bool:
	var keys := _retained_payload_keys()
	if keys.is_empty():
		return false
	var core := encode_payload_core(keys)
	if core == _rpc_last_reliable_core:
		return false
	_rpc_last_reliable_core = core
	return true


func _pack_wire_bytes(bytes: PackedByteArray) -> PackedByteArray:
	if compression_enabled and not bytes.is_empty():
		var compressed := bytes.compress(int(compression_mode))
		var output := PackedByteArray()
		output.resize(4)
		output.encode_u32(0, bytes.size())
		output.append_array(compressed)
		return output
	return bytes


func _unpack_wire_bytes(bytes: PackedByteArray) -> PackedByteArray:
	if compression_enabled and not bytes.is_empty():
		if bytes.size() < 4:
			Netw.dbg.error("PackedSynchronizer: Received compressed packet under 4 bytes.")
			return PackedByteArray()
		var decompressed_size := bytes.decode_u32(0)
		if decompressed_size > compression_max_size:
			Netw.dbg.error("PackedSynchronizer: Decompressed size exceeds safety limit.")
			return PackedByteArray()
		var compressed_data := bytes.slice(4)
		var unpacked := compressed_data.decompress(
			decompressed_size,
			int(compression_mode),
		)
		if unpacked.is_empty() and decompressed_size > 0:
			Netw.dbg.error("PackedSynchronizer: Decompression failed.")
		return unpacked
	return bytes


# Ordered payload virtual names (non-stamp virtuals) in config order, so both
# peers agree on the codec layout.
func _payload_keys() -> Array[StringName]:
	var stamps := _ordered_virtual_names()
	var out: Array[StringName] = []
	for vname: StringName in get_virtual_properties():
		if vname in stamps:
			continue
		out.append(vname)
	return out


func _retained_payload_keys() -> Array[StringName]:
	var out: Array[StringName] = []
	for key: StringName in _payload_keys():
		if get_property_replication_mode(key) == \
				SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE:
			out.append(key)
	return out


func _warn_retained_stock_props() -> void:
	if transport != Transport.STOCK:
		return
	for key: StringName in _retained_payload_keys():
		push_warning(
			(
					"PackedSynchronizer: retained property '%s' is on STOCK "
					+ "transport. Switch the property to volatile or move it "
					+ "to a plain synchronizer sibling."
			) % [key],
		)


# Per-key quantizers parallel to [param keys], null where unconfigured.
func _payload_quantizers(keys: Array[StringName]) -> Array:
	var out: Array = []
	for key: StringName in keys:
		out.append(property_codecs.get(key, null))
	return out


# Per-key live Variant types parallel to [param keys]. A decoder needs them
# because the wire omits the type tag for a quantized value.
func _payload_types(keys: Array[StringName]) -> Array:
	var out: Array = []
	for key: StringName in keys:
		out.append(typeof(_read_property(key, get_real_path(key))))
	return out


func _get_property_list() -> Array[Dictionary]:
	var result := super._get_property_list()
	if not Engine.is_editor_hint():
		return result
	var keys := _editor_codec_keys()
	if keys.is_empty():
		return result
	result.append(
		{
			"name": "Codecs",
			"type": TYPE_NIL,
			"usage": PROPERTY_USAGE_GROUP,
			"hint_string": "codec/",
		},
	)
	for key: StringName in keys:
		result.append(
			{
				"name": "codec/" + key,
				"type": TYPE_OBJECT,
				"usage": PROPERTY_USAGE_EDITOR,
				"hint": PROPERTY_HINT_RESOURCE_TYPE,
				"hint_string": "NetwQuantize",
			},
		)
	return result


func _get(property: StringName) -> Variant:
	if property.begins_with("codec/"):
		return property_codecs.get(StringName(property.trim_prefix("codec/")), null)
	return super._get(property)


func _set(property: StringName, value: Variant) -> bool:
	if property.begins_with("codec/"):
		set_property_codec(
			StringName(property.trim_prefix("codec/")),
			value as NetwQuantize,
		)
		notify_property_list_changed()
		return true
	return super._set(property, value)


func _validate_property(property: Dictionary) -> void:
	if property.name == "property_codecs":
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_STORAGE


# Payload property leaf names from the inspector replication_config, the
# edit-time analog of _payload_keys() (runtime registration has not run yet).
func _editor_codec_keys() -> Array[StringName]:
	var out: Array[StringName] = []
	if not replication_config:
		return out
	var stamps := _ordered_virtual_names()
	for path: NodePath in replication_config.get_properties():
		var sub := path.get_subname_count()
		if sub == 0:
			continue
		var leaf := StringName(path.get_subname(sub - 1))
		if leaf in stamps or leaf in out:
			continue
		out.append(leaf)
	return out
