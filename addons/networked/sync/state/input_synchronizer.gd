@tool
## Peer-authoritative input replication that records received input into the
## entity [NetwTimeline].
##
## Authority follows [member NetwEntity.controller], so the steering peer stamps
## and writes input and the server receives it. The stamp
## ([constant StampedSynchronizer.TICK]) and every payload property must share one
## lane, selected by [member stamp_mode]. The default volatile ALWAYS sync is the
## freshest-wins lane, since reliable input head-of-line blocks consumption behind
## a retransmit. ON_CHANGE trades that for the reliable, ordered delta, which never
## leaves a late packet in flight after a despawn.
##
## [codeblock]
## Inputs (authority = controller, bound by the entity lifecycle)
## └── InputSynchronizer           # ALWAYS sync: __tick, motion, ...
##       on server: records (tick, input) into the entity timeline
## [/codeblock]
##
## Input never carries forward in the timeline: a missing tick is a deliberate
## no-action, not a stale repeat. The single-sample packet is the first cut. The
## redundancy window that packs the last N tick samples is a later upgrade behind
## this same surface.
class_name InputSynchronizer
extends StampedSynchronizer

## Invoked on the server after an input packet is flushed, with the packet's
## [code](tick, input)[/code].
var on_input_received: Callable = Callable()

## Replication lane for the [constant StampedSynchronizer.TICK] stamp. It must
## match the payload lane in [member MultiplayerSynchronizer.replication_config]
## so the stamp stays coherent with the values it tags. ALWAYS is the volatile
## default, ON_CHANGE the reliable, ordered delta.
@export var stamp_mode: SceneReplicationConfig.ReplicationMode = \
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS


func configure() -> void:
	register_stamp(TICK, stamp_mode)


func record(tick: int, payload: Dictionary) -> void:
	if timeline:
		timeline.record_input(tick, payload)
	if on_input_received.is_valid():
		on_input_received.call(tick, payload)


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
