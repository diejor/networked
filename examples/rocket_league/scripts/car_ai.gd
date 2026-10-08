class_name RocketCarAI
extends Node3D

const TURN_DEADZONE := 0.1
const STUCK_SPEED := 1.0
const STUCK_SECONDS := 0.75
const REVERSE_SECONDS := 0.6

var stuck_for := 0.0
var reversing_for := 0.0

@onready var car: RocketCar = get_parent()
@onready var ball: RocketBall = NetwEntity.of(get_parent()).scene.root.get_node(
	^"ball",
)


func _ready() -> void:
	car.ai_enabled = true


func _input(event: InputEvent) -> void:
	# Disable AI if the player presses any key
	if event is InputEventKey and event.pressed:
		car.ai_enabled = false


func _process(delta: float) -> void:
	if not car.ai_enabled or not car.entity.is_controlled_locally:
		return

	# Calculate direction and distance to the ball
	var car_direction := global_transform.basis.z.normalized()
	var direction_to_ball := (ball.global_position - global_position).normalized()
	var cross := car_direction.cross(direction_to_ball).y

	var steering := 0.0
	if cross > TURN_DEADZONE:
		steering = -1.0 # Steer right
	elif cross < -TURN_DEADZONE:
		steering = 1.0 # Steer left

	if reversing_for > 0.0:
		reversing_for -= delta
		car.ai_motion = Vector2(-steering, -1.0)
		return
	var held := car.session.clock.tick < car.game.kickoff_tick
	var stalled := car.linear_velocity.length() < STUCK_SPEED
	stuck_for = stuck_for + delta if stalled and not held else 0.0
	if stuck_for > STUCK_SECONDS:
		stuck_for = 0.0
		reversing_for = REVERSE_SECONDS

	car.ai_motion = Vector2(steering, 1.0)

