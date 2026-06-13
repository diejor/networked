## Proto state synchronizer for the lag-comp spike (architecture P2).
##
## A [ProxySynchronizer] with authority [code]1[/code]. It supports two payload
## shapes so the spike can compare them under impairment:
## [br]- [b]split[/b] (default): ALWAYS [code]__tick[/code]/[code]__ack[/code]
##   stamps leading an ON_CHANGE watched [code]position[/code]. The shape the
##   architecture doc originally described.
## [br]- [b]bundled[/b]: one ALWAYS [code]__state[/code] dictionary carrying
##   tick, ack, and position together, so a packet is atomic.
##
## A.1 shows the split shape tears under jitter (the stamp and the watched
## payload are separate packets, independently delayed) while the bundled shape
## holds. Deleted when Phase 0 lands.
@tool
class_name SpikeStateSync
extends ProxySynchronizer

## When [code]true[/code], replicate one atomic [code]__state[/code] dictionary
## instead of split stamp + watched payload.
var bundled: bool = false

## When [code]true[/code], every prop (including [code]__tick[/code]) is
## ON_CHANGE, so all reconciliation state rides the one reliable, ordered delta
## stream together. This is the per-property-diff shape: coherent like bundled,
## but only changed props are sent.
var delta_mode: bool = false

## Server-side rarely-changing field (ON_CHANGE) used to show per-property
## diffing: it is delivered only on the ticks it actually changes.
var blink_value: int = 0

## Count of received [code]blink[/code] and [code]position[/code] writes, so a
## test can show the rare field arrives far less often than the hot one.
var blink_recv_count: int = 0
var position_recv_count: int = 0

## One entry per received state packet:
## [code]{ "tick": int, "ack": int, "position": Vector2 }[/code].
var captures: Array = []

## Server-side tick stamped together with the payload. The driver sets this and
## the body in the same [signal MultiplayerClock.on_tick] handler, so any torn
## pair a test sees is a transport fault, never a source-sampling race.
var authored_tick: int = -1

## Server-side value surfaced as the ack (last consumed input tick).
var server_ack: int = -1

## When set, received state routes into this timeline.
var timeline: SpikeTimeline = null

## Invoked on the owning client after a state packet lands, with the received
## [code](tick, ack, position)[/code].
var on_state_received: Callable = Callable()

## When [code]true[/code], the live body is written through on receive (remote
## display path). The owning client leaves this [code]false[/code] so prediction
## owns the body.
var write_through: bool = true

var _pending_tick: int = -1
var _pending_ack: int = -1


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	set_multiplayer_authority(1)
	if bundled:
		_register_always(&"__state")
	elif delta_mode:
		register_property(
			&"position",
			NodePath(".:position"),
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			false,
			true,
		)
		_register_watched(&"__tick")
		_register_watched(&"__ack")
		_register_watched(&"blink")
	else:
		register_property(
			&"position",
			NodePath(".:position"),
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			false,
			true,
		)
		_register_always(&"__tick")
		_register_always(&"__ack")
	finalize()


func _ordered_virtual_names() -> Array[StringName]:
	var order: Array[StringName] = []
	if bundled:
		order.append(&"__state")
	else:
		order.append(&"__tick")
		order.append(&"__ack")
	return order


func _register_watched(vname: StringName) -> void:
	if not _properties.has(vname):
		_properties[vname] = NodePath("")
		_prop_options[vname] = {
			"mode": SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			"spawn": false,
			"watch": true,
		}


func _register_always(vname: StringName) -> void:
	if not _properties.has(vname):
		_properties[vname] = NodePath("")
		_prop_options[vname] = {
			"mode": SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
			"spawn": false,
			"watch": false,
		}


func _body_position() -> Vector2:
	var root := get_node_or_null(root_path)
	return root.position if root is Node2D else Vector2.ZERO


func _read_property(prop: StringName, path: NodePath) -> Variant:
	if prop == &"__state":
		return {
			&"tick": authored_tick,
			&"ack": server_ack,
			&"position": _body_position(),
		}
	if prop == &"__tick":
		return authored_tick
	if prop == &"__ack":
		return server_ack
	if prop == &"blink":
		return blink_value
	return super._read_property(prop, path)


func _write_property(prop: StringName, path: NodePath, value: Variant) -> void:
	if prop == &"__state":
		var snap: Dictionary = value
		_receive(
			int(snap.get(&"tick", -1)),
			int(snap.get(&"ack", -1)),
			snap.get(&"position", Vector2.ZERO),
		)
		return
	if prop == &"__tick":
		_pending_tick = int(value)
		return
	if prop == &"__ack":
		_pending_ack = int(value)
		return
	if prop == &"blink":
		blink_recv_count += 1
		return
	if prop == &"position":
		position_recv_count += 1
		_receive(_pending_tick, _pending_ack, value)
		return
	super._write_property(prop, path, value)


func _receive(
		tick: int,
		ack: int,
		position: Variant,
) -> void:
	var clock := MultiplayerClock.for_node(self)
	var recv_tick := clock.tick if clock else -1
	captures.append(
		{
			"tick": tick,
			"ack": ack,
			"position": position,
			"recv_tick": recv_tick,
		},
	)
	if timeline != null and tick >= 0:
		timeline.record_state(tick, {&"position": position})
	if write_through:
		var root := get_node_or_null(root_path)
		if root is Node2D:
			root.position = position
	if on_state_received.is_valid() and tick >= 0:
		on_state_received.call(tick, ack, position)
