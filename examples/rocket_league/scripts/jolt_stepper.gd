class_name RocketJoltStepper
extends NetwPhysicsStepper

static func available() -> bool:
	return PhysicsServer3D.has_method(&"space_step")


static func schedule() -> NetwSimulation.Schedule:
	return NetwSimulation.SCHEDULE_STEPPED if available() \
	else NetwSimulation.SCHEDULE_FRAME


static func recovery_policy() -> NetwPrediction.RecoveryPolicy:
	return NetwPrediction.RECOVERY_POLICY_REBASE_REPLAY if available() \
	else NetwPrediction.RECOVERY_POLICY_REBASE_RECOVER


static func reconcile_mode() -> NetwPrediction.Reconcile:
	return NetwPrediction.RECONCILE_JOINT if available() \
	else NetwPrediction.RECONCILE_INDEPENDENT


func _can_step() -> bool:
	return available()


func _step(space: RID, delta: float) -> void:
	PhysicsServer3D.call(&"space_flush_queries", space)
	PhysicsServer3D.call(&"space_step", space, delta)
