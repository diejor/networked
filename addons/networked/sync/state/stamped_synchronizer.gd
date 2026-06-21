@tool
## [ProxySynchronizer] that stamps every packet with its authoring tick and
## flushes received snapshots into a [NetwTimeline].
##
## The stamp always rides the same replication stream as the payload it tags, so
## jitter can never pair a tick with the wrong values. Concrete subclasses pick
## the stream and the timeline side: [StateSynchronizer] puts the stamp and
## payload on the reliable ON_CHANGE delta and records state, [InputSynchronizer]
## puts them on the volatile ALWAYS sync and records input.
##
## [codeblock]
## # A subclass declares its stamps and payload in _configure; the base reads
## # __tick out on send and flushes each received packet into the timeline:
## func _configure() -> void:
##     set_multiplayer_authority(1)
##     _register_stamp(&"__tick", SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
##     register_property(&"position", NodePath(".:position"))
## [/codeblock]
##
## A packet is flushed once on the receiving peer, after Godot applies every
## property, via [signal MultiplayerSynchronizer.synchronized] and
## [signal MultiplayerSynchronizer.delta_synchronized]. The authority peer never
## receives its own packets, so [method _record] only ever runs on a consumer.
class_name StampedSynchronizer
extends ProxySynchronizer

## Virtual name of the authoring-tick stamp.
const TICK := &"__tick"

## Backing transport for the virtual-property surface.
enum Transport {
	## Godot [SceneMultiplayer] replication. The only implemented transport.
	STOCK,
}

## Selected transport. Only [constant Transport.STOCK] exists today. A future
## send-bytes state channel becomes a second value behind this same surface, so
## nothing above the synchronizer changes when it lands.
var transport := Transport.STOCK

## Destination for received snapshots. When null, packets are not recorded and
## the synchronizer behaves as a plain replicator.
var timeline: NetwTimeline = null

## When true, received payload writes through to the live node. The owning
## client sets this false in Phase 1 so prediction owns the body.
var write_through: bool = true

## Overrides the tick stamped onto outgoing packets. Negative reads the live
## [MultiplayerClock] tick instead.
var authored_tick: int = -1

## Authoring [constant TICK] of the most recently received packet, or
## [code]-1[/code] before the first. [MultiplayerInterpolator] reads this to key
## its history by the displayed authoring tick instead of the receive tick.
var last_received_tick: int = -1

## Maps a payload virtual name to the [NetwQuantize] that bit-packs it on the
## wire.
##
## A property absent from this map byte-aligns and writes a self-describing raw
## value, so quantization is purely additive over the stock path. The inspector
## exposes one [code]codec/<prop>[/code] slot per payload property and stores the
## choice here. The same [NetwQuantize] may back several properties by reference.
## [codeblock]
## state.set_property_codec(&"position", NetwQuantizeFixed.new())
## [/codeblock]
@export var property_codecs: Dictionary[StringName, NetwQuantize] = { }

var _pending_tick: int = -1
# Last-known value per payload virtual name. Never cleared, so an ON_CHANGE
# delta carrying only changed props still flushes a complete snapshot: unsent
# props carry forward from the previous packet.
var _pending_payload: Dictionary = { }
# Strategy that writes a restored payload onto the live body. The default writes
# every property straight through SynchronizersCache; a future physics impl swaps
# in to route the spatial subset through the PhysicsServer RID.
var _applicator := _BodyStateApplicator.new()


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	if not synchronized.is_connected(_on_synchronized):
		synchronized.connect(_on_synchronized)
	if not delta_synchronized.is_connected(_on_synchronized):
		delta_synchronized.connect(_on_synchronized)
	configure()
	finalize()


## Override to set authority and register stamps and payload before finalize.
func configure() -> void:
	pass


## Override to record [param payload] at [param tick] on the correct timeline
## side. Runs only on the receiving peer.
func record(_tick: int, _payload: Dictionary) -> void:
	pass


## Reads every payload virtual property into a [code]{vname: value}[/code]
## [Dictionary].
##
## The stamps ([method _ordered_virtual_names], i.e. [constant TICK] and any
## subclass ack) are excluded, so the result is exactly the snapshot
## [method record] stores and the receiving peer applies. Keys are stable across
## peers because they come from the same registered config, which is what lets a
## predicting client and the server simulate on identical input and compare state.
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


## Writes a [param snapshot] produced by [method snapshot_payload] straight onto
## the live node, ignoring keys that are not registered payload props.
##
## This is the reconciliation restore, so it bypasses [member write_through] and
## always lands on the body. The owning client keeps [member write_through] false
## so the network never snaps the predicted body, yet a correction still must.
func apply_payload(snapshot: Dictionary) -> void:
	var root := get_node_or_null(root_path)
	if not root:
		return
	_applicator.apply(self, root, snapshot)


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


# Returns the tick to stamp outgoing packets with.
func _authoring_tick() -> int:
	if authored_tick >= 0:
		return authored_tick
	var clock := MultiplayerClock.for_node(self)
	return clock.tick if clock else -1


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == TICK:
		return _authoring_tick()
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == TICK:
		_pending_tick = int(value)
		# Godot applies every property before emitting synchronized, so this is
		# set before any handler runs, regardless of connection order.
		last_received_tick = _pending_tick
		return
	_pending_payload[name] = value
	if write_through:
		super._write_property(name, path, value)


func _ordered_virtual_names() -> Array[StringName]:
	return [TICK]


func _on_synchronized() -> void:
	if _pending_tick < 0:
		return
	var snapshot := _pending_payload.duplicate()
	record(_pending_tick, snapshot)


func set_property_codec(vname: StringName, quantizer: NetwQuantize) -> void:
	if quantizer == null:
		property_codecs.erase(vname)
	else:
		property_codecs[vname] = quantizer


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
	result.append({
		"name": "Codecs",
		"type": TYPE_NIL,
		"usage": PROPERTY_USAGE_GROUP,
		"hint_string": "codec/",
	})
	for key: StringName in keys:
		result.append({
			"name": "codec/" + key,
			"type": TYPE_OBJECT,
			"usage": PROPERTY_USAGE_EDITOR,
			"hint": PROPERTY_HINT_RESOURCE_TYPE,
			"hint_string": "NetwQuantize",
		})
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


# Writes a restored payload onto the live body. The split between the spatial
# subset (transform, rotation, velocity) and the rest lives here so the future
# RID impl, which must route the spatial writes through the PhysicsServer so the
# solver cannot stomp the restored pose, is a drop-in with no call-site change.
# The default makes no distinction: every property is a plain property write.
class _BodyStateApplicator extends RefCounted:
	# Real-path leaf names that name a body's spatial state. The RID impl routes
	# these through PhysicsServer; the default treats them like any property.
	const SPATIAL: Array[StringName] = [
		&"position", &"global_position", &"transform", &"global_transform",
		&"rotation", &"global_rotation", &"quaternion", &"basis",
		&"linear_velocity", &"angular_velocity",
	]


	func apply(
			sync: StampedSynchronizer,
			root: Node,
			snapshot: Dictionary,
	) -> void:
		for vname: StringName in snapshot:
			if not sync.has_virtual_property(vname):
				continue
			var path := sync.get_real_path(vname)
			if _is_spatial(path):
				_apply_spatial(root, path, snapshot[vname])
			else:
				_apply_property(root, path, snapshot[vname])


	func _is_spatial(path: NodePath) -> bool:
		var count := path.get_subname_count()
		if count == 0:
			return false
		return path.get_subname(count - 1) in SPATIAL


	# Default spatial write is a plain property write. The RID impl overrides this.
	func _apply_spatial(root: Node, path: NodePath, value: Variant) -> void:
		_apply_property(root, path, value)


	func _apply_property(root: Node, path: NodePath, value: Variant) -> void:
		SynchronizersCache.assign_value(root, path, value)
