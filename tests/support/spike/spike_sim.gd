## Closed-form simulation step shared by server, predicting client, and replay.
##
## The lag-comp contract is one method,
## [code]_network_tick(input, delta, tick, is_fresh)[/code]. The spike keeps the
## body closed-form on purpose so every test can compute the expected state
## analytically and assert exact equality. No [method CharacterBody2D.move_and_slide]
## here, that is a separate spike (architecture risk 9.2). This isolates the
## transport and timeline modeling claim from the physics-replay claim.
class_name SpikeSim
extends RefCounted

const SPEED: float = 120.0


## Returns the motion vector encoded in [param input].
##
## Missing keys read as zero, so a neutral input yields no motion. This is the
## "missing input means no action" half of the carry-forward asymmetry.
static func motion_of(input: Dictionary) -> Vector2:
	return Vector2(
		float(input.get(&"mx", 0.0)),
		float(input.get(&"my", 0.0)),
	)


## Integrates [param position] by [param input] over [param delta].
static func integrate(
		position: Vector2,
		input: Dictionary,
		delta: float,
) -> Vector2:
	return position + motion_of(input) * SPEED * delta


## Returns [code]true[/code] when [param input] requests a one-shot fire.
static func wants_fire(input: Dictionary) -> bool:
	return bool(input.get(&"fire", false))
