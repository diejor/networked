class_name RocketCarAI
extends Node3D

# Stuff to get unstuck
const STUCK_THRESHOLD := 0.2
const STUCK_TIME := 4.0
const REVERSE_DURATION := 0.5
const TURN_DEADZONE := 0.1

@onready var car: RocketCar = get_parent()
@onready var ball: RocketBall = NetwEntity.of(get_parent()).scene.root.get_node(
	^"ball|0",
)

var last_position := Vector3.ZERO
var stuck_timer := 0.0
var reverse_timer := 0.0


func _ready() -> void:
	car.ai_enabled = true


func _input(event: InputEvent) -> void:
	# Disable AI if the player presses any key
	if event is InputEventKey and event.pressed:
		car.ai_enabled = false


func _process(delta: float) -> void:
	if not car.ai_enabled or not car.entity.is_controlled_locally:
		return

	if stuck_check(delta):
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

	car.ai_motion = Vector2(steering, 1.0)


func stuck_check(delta: float) -> bool:
	# Check if the car is stuck
	if global_position.distance_to(last_position) < STUCK_THRESHOLD:
		stuck_timer += delta
	else:
		stuck_timer = 0.0 # Reset the timer if were moving
	last_position = global_position

	# reverse if stuck
	if stuck_timer >= STUCK_TIME:
		reverse_timer = REVERSE_DURATION
	stuck_timer = minf(stuck_timer, STUCK_TIME)

	car.ai_jumping = false
	if reverse_timer <= 0.0:
		return false

	reverse_timer -= delta
	car.ai_motion = Vector2(0.0, -1.0) # Reverse out
	car.ai_jumping = true # Jump as well, why not.
	return true
