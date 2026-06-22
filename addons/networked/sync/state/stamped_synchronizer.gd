@tool
## [PackedSynchronizer] that stamps every packet with its authoring tick and
## flushes received snapshots into a [NetwTimeline].
##
## The stamp always rides the same replication stream as the payload it tags, so
## jitter can never pair a tick with the wrong values. Concrete subclasses pick
## the stream and the timeline side: [StateSynchronizer] puts the stamp and
## payload on the reliable ON_CHANGE delta and records state, [InputSynchronizer]
## puts them on the volatile ALWAYS sync and records input.
##
## [codeblock]
## # A subclass declares its stamps and payload in configure; the base reads
## # __tick out on send and flushes each received packet into the timeline:
## func configure() -> void:
##     set_multiplayer_authority(1)
##     register_stamp(&"__tick", SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
##     register_property(&"position", NodePath(".:position"))
## [/codeblock]
##
## A packet is flushed once on the receiving peer, after Godot applies every
## property, via [signal MultiplayerSynchronizer.synchronized] and
## [signal MultiplayerSynchronizer.delta_synchronized]. The authority peer never
## receives its own packets, so [method record] only ever runs on a consumer.
class_name StampedSynchronizer
extends PackedSynchronizer

## Virtual name of the authoring-tick stamp.
const TICK := &"__tick"

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
	super._ready()


## Override to record [param payload] at [param tick] on the correct timeline
## side. Runs only on the receiving peer.
func record(_tick: int, _payload: Dictionary) -> void:
	pass


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
