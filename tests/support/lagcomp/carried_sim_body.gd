## Closed-form predicted-entity root that declares a forward model for its
## position, the [method NetwScriptModel.PropertyConfig.carry_step] tier the
## engine accepts.
##
## The rule restates [method LagCompSimBody._network_tick]'s own arithmetic off
## the recorded transition rather than off the live node, which is what lets the
## engine replay it against the past the owner already recorded and judge it
## faithful. [member carry_gain] takes it off that arithmetic, so one fixture
## states both the rule the engine carries and the rule it refuses.
##
## [codeblock]
## var s := PredictionScenario.new()
## await s.setup(self)
## s.body_type = CarriedSimBody
## var p := await s.add_predicted_entity(
##     [&"position"],
##     [&"motion", &"bombing"],
##     PredictionComponent.MissingInput.STALL,
##     0.01,
##     PredictionComponent.Schedule.FRAME,
## )
## [/codeblock]
class_name CarriedSimBody
extends LagCompSimBody

## Distance past which a correction is a teleport rather than a repair.
##
## A carry advances the acknowledged value across every transition still in
## flight, and a rule landing further away than a teleport would move the body
## is refused whatever it computed. The entity default of two pixels is under a
## single frame of [constant ClosedFormSim.SPEED], so a body declaring none has
## every carry refused before its arithmetic is looked at.
const TELEPORT_AT := 200.0

## Factor the rule advances at, [code]1.0[/code] being the body's own step.
##
## Any other value makes the rule disagree with the transitions it is replayed
## against, which is the one fault the engine can see without being told.
var carry_gain: float = 1.0


func _init() -> void:
	super._init()
	Netw.configure_property(self, &"position") \
			.teleport_at(TELEPORT_AT) \
			.carry_step(_advance_position)


## Advances an acknowledged [param value] to the present by the motion
## [param ctx] recorded.
func _advance_position(
		value: Vector2,
		ctx: NetwPredictCarryContext,
) -> Vector2:
	var m: Vector2 = ctx.input.get(&"motion", Vector2.ZERO) * carry_gain
	return ClosedFormSim.integrate(
		value,
		{ &"mx": m.x, &"my": m.y },
		ctx.delta,
	)
