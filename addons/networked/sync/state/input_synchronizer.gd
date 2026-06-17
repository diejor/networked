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
## [SampleWindowCodec], sized by [method input_window_size] and sourced from the
## owning peer's predicted [NetwTimeline].
class_name InputSynchronizer
extends StampedSynchronizer

## Virtual name of the ack-bounded input redundancy window.
const INPUT_WINDOW := &"__input_window"

## Invoked on the server after an input packet is flushed, with the packet's
## [code](tick, input)[/code].
var on_input_received: Callable = Callable()

## Window size policy. [code]0[/code] derives the size from cadence (see
## [method redundancy_packets]); a positive value is an explicit sample count.
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
	register_stamp(TICK, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	if _window_enabled():
		register_stamp(INPUT_WINDOW, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)


# When the window carries input, the standalone payload props are redundant (the
# newest tick already rides inside the window), so suppress their replication.
# They stay registered so snapshot_payload() and the codec can still read them.
func finalize() -> void:
	super.finalize()
	if not _window_enabled() or not replication_config:
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


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == TICK:
		return _authoring_tick()
	if name == INPUT_WINDOW:
		return _build_input_window()
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == TICK:
		_pending_window.clear()
	if name == INPUT_WINDOW:
		_pending_window = SampleWindowCodec.decode(value, _payload_keys()) \
		if value is PackedByteArray else []
		return
	super._write_property(name, path, value)


func _ordered_virtual_names() -> Array[StringName]:
	if _window_enabled():
		return [TICK, INPUT_WINDOW]
	return [TICK]


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
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


func _record_one(tick: int, payload: Dictionary) -> void:
	if timeline:
		timeline.record_input(tick, payload)
	if on_input_received.is_valid():
		on_input_received.call(tick, payload)


# Encodes the unacked tail of the predicted timeline (capped to the effective
# window) into a compact PackedByteArray. With no timeline (or none recorded yet)
# it falls back to a single live sample at the authored tick, so the window is
# always a sole carrier even without prediction wiring. Empty only when the
# window is off or no tick has been authored.
func _build_input_window() -> PackedByteArray:
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
	return SampleWindowCodec.encode(samples, _payload_keys())


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


# Ordered payload virtual names (every virtual property that is not a stamp),
# preserving config insertion order so both peers agree on the codec layout.
func _payload_keys() -> Array[StringName]:
	var stamps := _ordered_virtual_names()
	var out: Array[StringName] = []
	for vname: StringName in get_virtual_properties():
		if vname in stamps:
			continue
		out.append(vname)
	return out


func _warn_if_undersized() -> void:
	if input_window_size <= 0:
		return
	var clock := MultiplayerClock.for_node(self)
	if not clock:
		return
	var recommended := maxi(2, ceili(replication_interval * clock.tickrate) * redundancy_packets)
	if input_window_size < recommended:
		push_warning(
			"InputSynchronizer: input_window_size=%d is below the cadence recommendation %d (replication_interval=%.3f, tickrate=%d, redundancy_packets=%d)" % [
				input_window_size,
				recommended,
				replication_interval,
				clock.tickrate,
				redundancy_packets,
			],
		)
