@tool
## Server-authoritative state replication that records snapshots into
## [NetwTimeline].
##
## Authority is always [code]1[/code], so only the server stamps and writes
## state. The payload rides [constant STATE], which frames
## [constant StampedSynchronizer.TICK] and [constant ACK] with the encoded
## [PackedSynchronizer] payload core.
##
## [codeblock]
## Player (CharacterBody2D, authority = 1)
## `-- StateSynchronizer
##     carrier: __state { __tick, __ack, payload }
##     owner client: record and reconcile
##     remote client: write through for display
## [/codeblock]
##
## Useful send cadence has a one tick floor. A sub-tick
## [member MultiplayerSynchronizer.replication_interval] burns packet framing
## without adding authoring information.
class_name StateSynchronizer
extends StampedSynchronizer

## Virtual name of the reconciliation ack (last consumed input tick).
const ACK := &"__ack"

## Virtual name of the bundled snapshot carrier.
const STATE := &"__state"

## Last consumed input tick surfaced to the owning client as [constant ACK].
## Phase 1's consume step sets this. Phase 0 leaves it at [code]-1[/code].
var server_ack: int = -1

## Invoked on the receiving client after a packet is flushed, with the packet's
## [code](tick, ack, payload)[/code]. Phase 1's prediction component connects
## here to drive reconciliation.
var on_state_received: Callable = Callable()

var _pending_ack: int = -1


func configure() -> void:
	set_multiplayer_authority(1)
	if transport == Transport.STOCK:
		register_stamp(STATE, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)


# State-sync presence is the rewind trigger: on the server, register the entity
# so the simulation service records its authoritative history every tick, even
# without a PredictionComponent. The server records via snapshot_payload(), never
# through record(), so timeline stays null here.
func _ready() -> void:
	super._ready()
	if not Engine.is_editor_hint():
		_register_timeline()


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var entity := NetwEntity.of(self)
	if entity and entity.reparenting and entity.reparenting.preserve_history:
		return
	var sim := _simulation()
	if entity and sim:
		sim.unregister_timeline(entity)


func _simulation() -> LagCompensation:
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return null
	return mt.get_service(LagCompensation) as LagCompensation


## Overrides [method PackedSynchronizer.carrier_name] to return
## [constant STATE].
func carrier_name() -> StringName:
	return STATE


func _ordered_virtual_names() -> Array[StringName]:
	if carrier_enabled() and transport == Transport.STOCK:
		return [STATE]
	return []


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == ACK:
		return server_ack
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == ACK:
		_pending_ack = int(value)
		return
	super._write_property(name, path, value)


## Overrides [method PackedSynchronizer.encode_carrier] to frame tick
## and ack ahead of the bit-packed payload core into one snapshot blob.
func encode_carrier() -> PackedByteArray:
	var keys := _payload_keys()
	return NetwCodec.encode_snapshot(
		_authoring_tick(),
		server_ack,
		snapshot_payload(),
		keys,
		_payload_quantizers(keys),
	)


## Overrides [method PackedSynchronizer.decode_carrier] to decode a
## snapshot blob and prime the receive path.
func decode_carrier(value: Variant) -> void:
	if not (value is PackedByteArray):
		return
	var keys := _payload_keys()
	var frame := NetwCodec.decode_snapshot(
		value,
		keys,
		_payload_quantizers(keys),
		_payload_types(keys),
	)
	if frame.is_empty():
		return
	var tick := int(frame.get(&"tick", -1))
	if transport == Transport.RPC and tick < last_received_tick:
		return
	_pending_tick = tick
	last_received_tick = tick
	_pending_ack = int(frame.get(&"ack", -1))
	var payload: Dictionary = frame.get(&"payload", { })
	for k: StringName in payload:
		super._write_property(k, get_real_path(k), payload[k])


## Overrides [method StampedSynchronizer.record] to record [param payload]
## at [param tick] in the state [member StampedSynchronizer.timeline] on the
## client.
##
## Also invokes the [member on_state_received] callback to trigger prediction
## reconciliation.
func record(tick: int, payload: Dictionary) -> void:
	if timeline:
		timeline.record_state(tick, payload)
	if on_state_received.is_valid():
		on_state_received.call(tick, _pending_ack, payload)


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED:
		var entity := NetwEntity.of(self)
		if entity:
			entity.state = self
			if not entity.control_changed.is_connected(_repin_authority):
				entity.control_changed.connect(_repin_authority)
			if not entity.reparented.is_connected(_on_reparented):
				entity.reparented.connect(_on_reparented)


func _on_reparented(_reparent: MultiplayerEntity.ReparentOpts) -> void:
	if Engine.is_editor_hint():
		return
	_register_timeline()


func _register_timeline() -> void:
	if multiplayer and multiplayer.is_server():
		var entity := NetwEntity.of(self)
		if entity:
			var sim := LagCompensation.resolve_required(self)
			if sim:
				sim.register_timeline(entity)


# Server Authority Protection: stay authority 1 regardless of the parent
# entity's recursive controller updates.
func _repin_authority(_previous_peer: int, _peer: int) -> void:
	set_multiplayer_authority(1)
	_refresh_rpc_driver()


func _rpc_outgoing_bytes(tick: int) -> PackedByteArray:
	var core := encode_payload_core()
	var retained_changed := _retained_changed()
	var heartbeat_due := _rpc_heartbeat_due(tick)
	if core == _rpc_last_core and not retained_changed and not heartbeat_due:
		return PackedByteArray()
	_rpc_last_core = core
	_rpc_force_reliable = retained_changed
	return _pack_wire_bytes(encode_carrier())


func _on_rpc_carrier_flushed() -> void:
	_on_synchronized()


func _rpc_heartbeat_due(tick: int) -> bool:
	if _rpc_last_sent_tick < 0:
		return true
	var interval := heartbeat_ticks
	if interval <= 0:
		var clock := MultiplayerClock.for_node(self)
		interval = clock.tickrate if clock else 60
	return tick - _rpc_last_sent_tick >= interval


func relay_channel() -> int:
	return RelayService.Command.STATE
