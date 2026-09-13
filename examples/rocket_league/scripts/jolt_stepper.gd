class_name RocketJoltStepper
extends NetwPhysicsStepper

static func available() -> bool:
	return PhysicsServer3D.has_method(&"space_step")


static func schedule() -> NetwPredict.Schedule:
	return NetwPredict.SCHEDULE_STEPPED if available() \
	else NetwPredict.SCHEDULE_FRAME


func _can_step() -> bool:
	return available()


func _step(space: RID, delta: float) -> void:
	PhysicsServer3D.call(&"space_flush_queries", space)
	PhysicsServer3D.call(&"space_step", space, delta)
