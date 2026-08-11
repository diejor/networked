## Contract base for a physics backend that can re-run a space step inside one
## frame.
##
## No public symbol here names an engine, so one seam admits any backend that
## can re-step. A space with no installed stepper cannot replay, and a member
## declaring [constant NetwPredict.Schedule.STEPPED] runs as
## [constant NetwPredict.Schedule.FRAME] instead.
##
## [codeblock]
##     class SpaceStepper extends NetwPhysicsStepper:
##         func _can_step() -> bool:
##             return PhysicsServer3D.has_method(&"space_step")
##
##         func _step(space: RID, delta: float) -> void:
##             PhysicsServer3D.call(&"space_step", space, delta)
##
##     api.predict_stepper_install(space, SpaceStepper.new())
## [/codeblock]
class_name NetwPhysicsStepper
extends RefCounted


# Capability probe, read once by an install so a driver that cannot step is
# refused there rather than at the first replayed transition.
func _can_step() -> bool:
	return false


# Re-runs one step of the space with the transition's delta.
func _step(space: RID, delta: float) -> void:
	pass


# Records the space's state at the tick, for a later restore.
func _snapshot(space: RID, tick: int) -> void:
	pass


# Restores the space to the state recorded for the tick.
func _restore(space: RID, tick: int) -> void:
	pass
