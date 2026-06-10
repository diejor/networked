## Minimal [ProxySynchronizer] that replicates [member Node2D.position].
class_name ProbeProxy
extends ProxySynchronizer

func _ready() -> void:
	if Engine.is_editor_hint():
		return
	register_property(
		&"position",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		false,
		true,
	)
	finalize()
