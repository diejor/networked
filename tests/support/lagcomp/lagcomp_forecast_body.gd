## Closed-form predicted body that replicates velocity, for the extrapolated snap
## restore scenarios.
##
## It extends [LagCompSimBody] with a replicated [member velocity] state field and
## declares [code]position[/code] with a
## [method NetwScriptModel.PropertyConfig.carry_forward] velocity channel, so an
## [constant PredictionComponent.RestoreMode.EXTRAPOLATED] correction carries the
## body forward by its replicated velocity instead of snapping to the stale
## authoritative tick. The velocity is the closed-form position derivative, so the
## projection is exact for the constant-input runs the rig scripts.
class_name LagCompForecastBody
extends LagCompSimBody

## Replicated velocity in units per second, the derivative the extrapolated restore
## projects [code]position[/code] by.
var velocity: Vector2 = Vector2.ZERO


# Adds velocity to the state set and names it as position's recovery channel. The
# base _init still marks position and the input fields through super().
func _init() -> void:
	super()
	Netw.configure_property(self, &"velocity").state()
	Netw.configure_property(self, &"position").carry_along(&"velocity") \
			.interpolate(NetwInterpolate.new().lerp().project_by(&"velocity"))


# Runs the base closed-form step, then records the per-tick position derivative as
# the replicated velocity so an extrapolated restore reads a truthful rate.
func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
	var before := position
	super(delta, tick, is_fresh)
	velocity = (position - before) / delta if delta > 0.0 else Vector2.ZERO
