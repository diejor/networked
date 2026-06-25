@tool
## Server-authoritative state replication that records authoritative snapshots
## into the entity [NetwTimeline].
##
## Authority is always 1, so only the server stamps and writes state. The stamp
## ([constant StampedSynchronizer.TICK]), the reconciliation ack
## ([constant ACK]), and every payload property share one reliable, ordered
## ON_CHANGE delta packet, so a client knows the authoring tick before applying
## the values and the stamp can never tear away from them.
##
## [codeblock]
## Player (CharacterBody2D, authority = 1)
## └── StateSynchronizer            # ON_CHANGE delta: __tick, __ack, position, ...
##       on owner client: records into timeline, hands (tick, ack) to prediction
##       on remote client: writes through; the interpolator displays it
## [/codeblock]
##
## [constant ACK] is the reconciliation ack (server to client, last consumed
## input tick). It is per-entity and well defined because each entity has at most
## one input-owning peer.
class_name StateSynchronizer
extends StampedSynchronizer

## Virtual name of the reconciliation ack (last consumed input tick).
const ACK := &"__ack"

## Virtual name of the bundled snapshot carrier used when
## [member PackedSynchronizer.bundle_payload] is on.
##
## It frames [constant StampedSynchronizer.TICK] and [constant ACK] ahead of the
## [method PackedSynchronizer.encode_carrier] payload core, so the whole snapshot
## is one atomic [NetwCodec] blob on the volatile ALWAYS lane instead of separate
## ON_CHANGE stamps. Bundling is where [member PackedSynchronizer.property_codecs]
## quantization pays off, since the blob is one opaque property.
const STATE := &"__state"

## Last consumed input tick surfaced to the owning client as [constant ACK].
## Phase 1's consume step sets this; Phase 0 leaves it at -1.
var server_ack: int = -1

## Invoked on the receiving client after a packet is flushed, with the packet's
## [code](tick, ack, payload)[/code]. Phase 1's prediction component connects
## here to drive reconciliation.
var on_state_received: Callable = Callable()

var _pending_ack: int = -1


func configure() -> void:
	set_multiplayer_authority(1)
	if bundle_payload:
		register_stamp(STATE, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
		return
	register_stamp(TICK, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	register_stamp(ACK, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


# State-sync presence is the rewind trigger: on the server, register the entity
# so the simulation service records its authoritative history every tick, even
# without a PredictionComponent. The server records via snapshot_payload(), never
# through record(), so timeline stays null here (the server never receives its
# own packets).
func _ready() -> void:
	super._ready()
	if Engine.is_editor_hint():
		return
	if multiplayer and multiplayer.is_server():
		var entity := NetwEntity.of(self)
		# State-sync presence is the rewind trigger, so an entity-bound synchronizer
		# needs the tree's LagCompensation node. The required guard logs a clear error
		# when this synchronizer sits under a MultiplayerTree with no node mounted, yet
		# stays quiet for a scene run standalone (no enclosing tree, e.g. pressing F6).
		if entity:
			var sim := LagCompensation.resolve_required(self)
			if sim:
				sim.register_timeline(entity)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	var entity := NetwEntity.of(self)
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
	if bundle_payload:
		return [STATE]
	return [TICK, ACK]


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == STATE:
		return encode_carrier()
	if name == ACK:
		return server_ack
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == STATE:
		decode_carrier(value)
		return
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
##
## This updates the stamp, the pending ack, and each payload value (routed
## through the stamp layer so write_through and _pending_payload behave
## exactly as the unbundled path).
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
	_pending_tick = int(frame.get(&"tick", -1))
	last_received_tick = _pending_tick
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


# Server Authority Protection: stay authority 1 regardless of the parent
# entity's recursive controller updates.
func _repin_authority(_previous_peer: int, _peer: int) -> void:
	set_multiplayer_authority(1)
