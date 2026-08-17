## Test double recording [NetwEntity] lifecycle-signal ordering relative to
## this component's own tree notifications.
class_name SpawnIdentityProbeEntity
extends Node

@export var identity_packet: Dictionary = { }

var samples: Array[Dictionary] = []


func _notification(what: int) -> void:
	if what != NOTIFICATION_PARENTED or Engine.is_editor_hint():
		return
	_record_sample(&"parented")
	var entity := NetwEntity.resolve(self)
	if not entity:
		return
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	Netw.configure_property(self, &"identity_packet").on_spawn()
	if not entity.spawning.is_connected(_on_spawning):
		entity.spawning.connect(_on_spawning)


func _enter_tree() -> void:
	_record_sample(&"enter_tree_before_super")
	_record_sample(&"enter_tree_after_super")


func _ready() -> void:
	_record_sample(&"ready_before_super")
	_record_sample(&"ready_after_super")


func _on_spawning() -> void:
	_record_sample(&"spawning")


func has_marker_at(stage: StringName, marker: String) -> bool:
	for sample: Dictionary in samples:
		if sample.get("stage", &"") != stage:
			continue
		var packet := sample.get("packet", { }) as Dictionary
		return packet.get("marker", "") == marker
	return false


func _record_sample(stage: StringName) -> void:
	samples.append(
		{
			"stage": stage,
			"packet": identity_packet.duplicate(true),
		},
	)
