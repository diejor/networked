class_name RocketJoltStepper
extends NetwPhysicsStepper

static func available() -> bool:
	return PhysicsServer3D.has_method(&"space_step")


static func schedule() -> NetwSimulationHandle.Schedule:
	return NetwSimulationHandle.SCHEDULE_STEPPED if available() \
	else NetwSimulationHandle.SCHEDULE_FRAME


static func recovery_policy() -> NetwPredict.RecoveryPolicy:
	return NetwPredict.RECOVERY_POLICY_REBASE_REPLAY if available() \
	else NetwPredict.RECOVERY_POLICY_REBASE_RECOVER


static func reconcile_mode() -> NetwPredict.Reconcile:
	return NetwPredict.RECONCILE_JOINT if available() \
	else NetwPredict.RECONCILE_INDEPENDENT


func _can_step() -> bool:
	return available()


func _step(space: RID, delta: float) -> void:
	PhysicsServer3D.call(&"space_flush_queries", space)
	PhysicsServer3D.call(&"space_step", space, delta)
