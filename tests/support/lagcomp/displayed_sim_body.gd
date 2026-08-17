## A [LagCompSimBody] whose predicted state is also a displayed track.
##
## Adding the display declaration is what makes the entity's display role a
## question at all: without a tracked value there is no runtime to resolve, and
## the [member NetwPredictionHandle.sim_mode] rung of the role ladder is never
## reached by a real engine.
class_name DisplayedSimBody
extends LagCompSimBody

func _init() -> void:
	super()
	Netw.configure_property(self, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.05).to(&"position"),
	)
