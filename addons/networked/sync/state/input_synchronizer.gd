@tool
## Peer-authoritative input replication that records received input into the
## entity [NetwTimeline].
##
## Authority follows [member NetwEntity.controller], so the steering peer stamps
## and writes input on the volatile ALWAYS lane and the server receives it.
## Reliable input head-of-line blocks consumption behind a retransmit, so this
## synchronizer keeps input on the freshest-wins lane.
##
## [codeblock]
## Inputs (authority = controller, bound by the entity lifecycle)
## └── InputSynchronizer           # ALWAYS sync: __tick, __input_window
##       on server: records (tick, input) into the entity timeline
## [/codeblock]
##
## Input never carries forward in the timeline: a missing tick is a deliberate
## no-action, not a stale repeat. [constant INPUT_WINDOW] carries recent unacked
## samples on the same volatile lane as a compact [PackedByteArray] encoded by
## [NetwCodec], sized by [method input_window_size] and sourced from the
## owning peer's predicted [NetwTimeline].
##
## Useful send cadence has a one tick floor. A sub-tick
## [member MultiplayerSynchronizer.replication_interval] burns packet framing
## without adding authoring information.
class_name InputSynchronizer
extends StampedSynchronizer

## Virtual name of the ack-bounded input redundancy window.
const INPUT_WINDOW := &"__input_window"

## Peer audience for controller-authored input.
enum Audience {
	## Replicate input only to server authority.
	SERVER_ONLY,
	## Replicate input to every visible peer.
	PUBLIC,
}

## Invoked on the server after an input packet is flushed, with the packet's
## [code](tick, input)[/code].
var on_input_received: Callable = Callable()

## Visibility policy for controller-authored input.
##
## [constant Audience.SERVER_ONLY] keeps raw input on the controller to server
## stream. [constant Audience.PUBLIC] leaves [member public_visibility] enabled
## for projects that intentionally let peers consume other peers' input.
## [codeblock]
## input.audience = InputSynchronizer.Audience.SERVER_ONLY
## [/codeblock]
@export var audience: Audience = Audience.SERVER_ONLY:
	set(value):
		audience = value
		_apply_audience()

## Window size policy. [code]0[/code] derives the size from cadence (see
## [method redundancy_packets]). A positive value is an explicit sample count.
## [code]1[/code] disables the window so the payload props carry input directly.
@export_range(0, 32, 1) var input_window_size: int = 0

## Consecutive lost input packets the derived window should tolerate when
## [member input_window_size] is [code]0[/code]. The derived size is
## [code]ceil(replication_interval * tickrate) * redundancy_packets[/code].
@export_range(1, 8, 1) var redundancy_packets: int = 2

## Last input tick the server acknowledged through [constant StateSynchronizer.ACK].
## Floors the window so acknowledged samples are not re-sent.
var acknowledged_tick: int = -1:
	set(value):
		acknowledged_tick = maxi(acknowledged_tick, value)

var _pending_window: Array = []


func configure() -> void:
	if transport == Transport.RPC and audience == Audience.PUBLIC:
		push_error(
			"InputSynchronizer: Audience.PUBLIC requires STOCK transport.",
		)
	if transport == Transport.STOCK:
		register_stamp(TICK, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	if _window_enabled() and transport == Transport.STOCK:
		register_stamp(INPUT_WINDOW, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)


# The base suppresses the windowed payload props to NEVER (the newest tick
# already rides inside the window). This only adds the cadence sizing warning.
func finalize() -> void:
	super.finalize()
	if _window_enabled():
		_warn_if_undersized()


func record(tick: int, payload: Dictionary) -> void:
	var recorded_pending := false
	for entry: Dictionary in _pending_window:
		var sample_tick := int(entry.get(&"tick", -1))
		var sample := entry.get(&"input", { })
		if sample_tick < 0 or not (sample is Dictionary):
			continue
		_record_one(sample_tick, sample)
		if sample_tick == tick:
			recorded_pending = true
	if not recorded_pending:
		_record_one(tick, payload)
	_pending_window.clear()


## Overrides [method PackedSynchronizer.carrier_enabled] to gate the
## bundled carrier by checking whether the input window is enabled.
func carrier_enabled() -> bool:
	return _window_enabled()


## Overrides [method PackedSynchronizer.carrier_name] to return
## [constant INPUT_WINDOW].
func carrier_name() -> StringName:
	return INPUT_WINDOW


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == TICK:
		_pending_window.clear()
	super._write_property(name, path, value)


func _ordered_virtual_names() -> Array[StringName]:
	if _window_enabled():
		return [TICK, INPUT_WINDOW]
	return [TICK]


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		_apply_audience()
		var entity := NetwEntity.of(self)
		if entity:
			entity.input = self
			_bind_authority(entity)
			if not entity.control_changed.is_connected(_on_control_changed):
				entity.control_changed.connect(_on_control_changed)
			if not entity.spawning.is_connected(_on_spawning):
				entity.spawning.connect(_on_spawning)


func _on_spawning() -> void:
	var entity := NetwEntity.of(self)
	if entity:
		_bind_authority(entity)


func _on_control_changed(_previous_peer: int, _peer: int) -> void:
	var entity := NetwEntity.of(self)
	if entity:
		_bind_authority(entity)


# Authority is the controller, or the server (1) for a server-controlled entity.
func _bind_authority(entity: NetwEntity) -> void:
	var controller := entity.controller
	set_multiplayer_authority(controller if controller != 0 else 1)
	_refresh_rpc_driver()


func _apply_audience() -> void:
	if audience == Audience.PUBLIC:
		public_visibility = true
		update_visibility()
		return
	public_visibility = false
	set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
	update_visibility()


func _record_one(tick: int, payload: Dictionary) -> void:
	if timeline:
		timeline.record_input(tick, payload)
	if on_input_received.is_valid():
		on_input_received.call(tick, payload)


## Overrides [method PackedSynchronizer.encode_carrier] to pack the
## unacked tail of the predicted timeline (capped to the effective
## window) into a compact [PackedByteArray].
##
## With no timeline (or none recorded yet) it falls back to a single live
## sample at the authored tick, so the window is always a sole carrier
## even without prediction wiring. Empty only when the window is off or
## no tick has been authored.
func encode_carrier() -> PackedByteArray:
	if not _window_enabled():
		return PackedByteArray()
	var newest := _authoring_tick()
	if newest < 0:
		return PackedByteArray()
	var samples: Array[Dictionary] = []
	if timeline:
		var from := maxi(acknowledged_tick + 1, newest - _effective_window_size() + 1)
		samples = timeline.inputs_in_range(from, newest)
	if samples.is_empty():
		samples = [{ &"tick": newest, &"input": snapshot_payload() }]
	var keys := _payload_keys()
	return NetwCodec.encode_window(samples, keys, _payload_quantizers(keys))


## Overrides [method PackedSynchronizer.decode_carrier] to decode a
## received window blob into the pending samples that [method record]
## flushes.
func decode_carrier(value: Variant) -> void:
	if value is PackedByteArray:
		var keys := _payload_keys()
		var decoded := NetwCodec.decode_window(
			value,
			keys,
			_payload_quantizers(keys),
			_payload_types(keys),
		)
		_pending_window = []
		var newest := -1
		for entry: Dictionary in decoded:
			newest = maxi(newest, int(entry.get(&"tick", -1)))
		if newest <= _rpc_last_received_tick:
			return
		_rpc_last_received_tick = newest
		_pending_tick = newest
		last_received_tick = newest
		for entry: Dictionary in decoded:
			if int(entry.get(&"tick", -1)) > acknowledged_tick:
				_pending_window.append(entry)
	else:
		_pending_window = []


func _rpc_outgoing_bytes(tick: int) -> PackedByteArray:
	var newest := _authoring_tick()
	if newest < 0 or newest == _rpc_last_sent_tick:
		return PackedByteArray()
	_rpc_force_reliable = false
	return _pack_wire_bytes(encode_carrier())



func _on_rpc_carrier_flushed() -> void:
	_on_synchronized()


# The window is on unless input_window_size explicitly requests single-sample
# mode. Keyed off the raw export so it holds before a clock exists.
func _window_enabled() -> bool:
	return input_window_size != 1


# Effective sample count: explicit when input_window_size > 0, else derived from
# send cadence (replication_interval) against the tick rate, scaled by
# redundancy_packets. Falls back to 4 when no clock is resolvable yet.
func _effective_window_size() -> int:
	if input_window_size > 0:
		return input_window_size
	var clock := MultiplayerClock.for_node(self)
	if clock:
		return maxi(2, ceili(replication_interval * clock.tickrate) * redundancy_packets)
	return 4


func _warn_if_undersized() -> void:
	if input_window_size <= 0:
		return
	var clock := MultiplayerClock.for_node(self)
	if not clock:
		return
	var recommended := maxi(2, ceili(replication_interval * clock.tickrate) * redundancy_packets)
	if input_window_size < recommended:
		push_warning(
			(
					"InputSynchronizer: input_window_size=%d is below the "
					+ "cadence recommendation %d (replication_interval=%.3f, "
					+ "tickrate=%d, redundancy_packets=%d)"
			) % [
				input_window_size,
				recommended,
				replication_interval,
				clock.tickrate,
				redundancy_packets,
			],
		)


func relay_channel() -> int:
	return RelayService.Command.INPUT


func relay_recipients(liveness: LivenessService, entity: NetwEntity) -> Array[int]:
	if audience == Audience.SERVER_ONLY:
		if multiplayer and not multiplayer.is_server():
			return [1]
		return []
	return super.relay_recipients(liveness, entity)
