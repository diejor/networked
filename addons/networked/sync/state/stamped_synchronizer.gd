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

var _pending_tick: int = -1
# Last-known value per payload virtual name. Never cleared, so an ON_CHANGE
# delta carrying only changed props still flushes a complete snapshot: unsent
# props carry forward from the previous packet.
var _pending_payload: Dictionary = { }


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
	for vname: StringName in snapshot:
		if not has_virtual_property(vname):
			continue
		SynchronizersCache.assign_value(root, get_real_path(vname), snapshot[vname])


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
