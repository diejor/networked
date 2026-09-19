extends GPUParticles3D

func _ready() -> void:
	emitting = true
	await get_tree().create_timer(lifetime).timeout
	queue_free()
