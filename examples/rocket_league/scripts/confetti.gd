extends GPUParticles3D

func _ready() -> void:
	emitting = true
	finished.connect(queue_free)
