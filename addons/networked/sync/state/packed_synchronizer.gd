@tool
## [ProxySynchronizer] that bit-packs its payload into one [NetwCodec] blob on the
## wire, quantized by a per-property [NetwQuantize].
##
## Godot's stock replication has no per-property codec hook, so a quantizer only
## reaches the wire when the values are bundled into a single carrier property and
## encoded here. [member bundle_payload] off leaves the codecs inert and the
## payload on stock per-property replication. On, the payload props are suppressed
## to [constant SceneReplicationConfig.REPLICATION_MODE_NEVER] and ride one
## [method carrier_name] blob instead, trading Godot's ON_CHANGE per-property
## diffing for an atomic packet. Bundle co-changing hot props, leave sparse
## independent props on the stock path.
##
## [codeblock]
## # A standalone quantized state sync for many server-driven AI agents:
## var sync := PackedSynchronizer.new()
## sync.bundle_payload = true
## sync.register_property(&"position", NodePath(".:position")).quantize(pos_codec)
## sync.register_property(&"velocity", NodePath(".:velocity")).quantize(vel_codec)
## # position + velocity now ride one __packed blob, no timeline, no prediction.
## [/codeblock]
##
## The wire never carries property names. Both peers derive the ordered key list
## and the per-key [NetwQuantize] from the same registered config, which is what
## lets [method snapshot_payload] and the receive path agree without a schema.
class_name PackedSynchronizer
extends ProxySynchronizer

## Virtual name of the bare bundled-payload carrier.
const PACKED := &"__packed"

## Backing transport for the virtual-property surface.
enum Transport {
	## Godot [SceneMultiplayer] replication. The only implemented transport.
	STOCK,
}

## Selected transport. Only [constant Transport.STOCK] exists today. A future
## send-bytes channel becomes a second value behind this same surface: it delivers
## the same packed blob through [code]rpc_id[/code] instead of a replicated
## property, so nothing above the synchronizer changes when it lands.
var transport := Transport.STOCK

## When true, every payload property is packed into one [method carrier_name]
## blob on the volatile ALWAYS lane instead of replicating per property.
##
## Bundling is the only way a [member property_codecs] quantizer reaches the wire,
## so the codecs are inert while this is off. Off by default, which preserves the
## per-property replication shape for existing synchronizers.
@export var bundle_payload: bool = false

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


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	configure()
	finalize()


## Override to set authority and register stamps and payload before finalize.
##
## The base registers the bare [method carrier_name] stamp when
## [member bundle_payload] is on, so a standalone [PackedSynchronizer] configured
## through the inspector bundles with no override.
func configure() -> void:
	if carrier_enabled():
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


## Override to gate the bundled carrier. The base returns [member bundle_payload].
func carrier_enabled() -> bool:
	return bundle_payload


## Override to name the bundled carrier virtual property. The base returns
## [constant PACKED].
func carrier_name() -> StringName:
	return PACKED


## Encodes the carrier blob. The base packs the bare payload with no header.
##
## Subclasses override to prepend their framing (a tick stamp, an ack, or a
## redundancy window) ahead of the same [method NetwCodec.encode_payload] core.
func encode_carrier() -> PackedByteArray:
	var keys := _payload_keys()
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.encode_payload(w, snapshot_payload(), keys, _payload_quantizers(keys))
	return w.to_bytes()


## Decodes the carrier blob and writes each payload value onto the live node. The
## base reads the bare payload written by [method encode_carrier].
func decode_carrier(value: Variant) -> void:
	if not (value is PackedByteArray):
		return
	var keys := _payload_keys()
	var r := NetwBitBuffer.Reader.new(value)
	var payload := NetwCodec.decode_payload(
		r,
		keys,
		_payload_quantizers(keys),
		_payload_types(keys),
	)
	for k: StringName in payload:
		super._write_property(k, get_real_path(k), payload[k])


func _ordered_virtual_names() -> Array[StringName]:
	if carrier_enabled():
		return [carrier_name()]
	return []


func _read_property(name: StringName, path: NodePath) -> Variant:
	if carrier_enabled() and name == carrier_name():
		return encode_carrier()
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if carrier_enabled() and name == carrier_name():
		decode_carrier(value)
		return
	super._write_property(name, path, value)


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
