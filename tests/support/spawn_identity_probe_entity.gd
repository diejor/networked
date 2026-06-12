class_name SpawnIdentityProbeEntity
extends MultiplayerEntity

@export var identity_packet: Dictionary = { }

var samples: Array[Dictionary] = []


func _notification(what: int) -> void:
	if what == NOTIFICATION_PARENTED and not Engine.is_editor_hint():
		_record_sample(&"parented_before_super")
		var entity := Netw.ctx(self).entity
		if entity:
			entity.contribute_spawn_property(self, &"identity_packet")
			if not entity.owner_tree_entered.is_connected(
					_on_owner_tree_entered,
			):
				entity.owner_tree_entered.connect(_on_owner_tree_entered)
	super._notification(what)
	if what == NOTIFICATION_PARENTED and not Engine.is_editor_hint():
		if not spawning.is_connected(_on_spawning):
			spawning.connect(_on_spawning)
		_record_sample(&"parented_after_super")


func _enter_tree() -> void:
	_record_sample(&"enter_tree_before_super")
	super._enter_tree()
	_record_sample(&"enter_tree_after_super")


func _ready() -> void:
	_record_sample(&"ready_before_super")
	super._ready()
	_record_sample(&"ready_after_super")


func _on_owner_tree_entered() -> void:
	_record_sample(&"owner_tree_entered")


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
			"entity_id": entity_id,
			"peer_id": peer_id,
			"root_path": root_path,
		},
	)
