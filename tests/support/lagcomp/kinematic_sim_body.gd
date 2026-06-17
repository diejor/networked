## Real-physics CharacterBody2D fixture for the move_and_slide replay regression.
##
## Mirrors examples/bomber player.gd's motion (velocity = motion * SPEED, then
## move_and_slide) under the frozen [code]_network_tick[/code] contract, so the
## fixture exercises the exact physics bomber replays during reconciliation. Built
## in code with a rectangle collider, no authored scene.
class_name KinematicSimBody
extends CharacterBody2D

## Matches bomber's MOTION_SPEED so the fixture's numbers transfer directly.
const SPEED := 90.0

var _half: Vector2


func _init(half_extents: Vector2 = Vector2(8, 8)) -> void:
	_half = half_extents
	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = half_extents * 2.0
	shape.shape = rect
	add_child(shape)


## The permanent simulation contract (architecture §4.4). The [param delta] arg
## is carried but unused here: move_and_slide integrates with
## get_physics_process_delta_time() internally, which is exactly the rate
## coupling test_move_and_slide_uses_physics_delta_not_arg pins down.
func _network_tick(
		input: Dictionary,
		_delta: float,
		_tick: int,
		_is_fresh: bool,
) -> void:
	velocity = (input.get(&"motion", Vector2.ZERO) as Vector2) * SPEED
	move_and_slide()


## Captures the reconcilable spatial state. Reconciliation restores exactly this
## dictionary, then replays pending inputs over it.
func snapshot() -> Dictionary:
	return { &"position": position, &"velocity": velocity }


## Restores a prior [method snapshot]. Note: is_on_floor() stays stale until the
## next move_and_slide, so any flag read must happen after a replayed tick.
func restore(snap: Dictionary) -> void:
	position = snap[&"position"]
	velocity = snap[&"velocity"]
