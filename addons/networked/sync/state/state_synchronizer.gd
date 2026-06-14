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
	register_stamp(TICK, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	register_stamp(ACK, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)


func _ordered_virtual_names() -> Array[StringName]:
	return [TICK, ACK]


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == ACK:
		return server_ack
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == ACK:
		_pending_ack = int(value)
		return
	super._write_property(name, path, value)


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
