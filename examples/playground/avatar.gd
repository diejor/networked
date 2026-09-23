class_name Avatar
extends CharacterBody3D

const SPEED := 4.0
const THROW_SPEED := 7.0

var motion := Vector2.ZERO
var held: PlayCube
var pressed := {
	&"left": false, &"right": false, &"forward": false, &"back": false,
}

var entity: NetwEntity

@onready var hand: Node3D = $Hand
@onready var reach: RayCast3D = $Hand/Reach
@onready var camera: Camera3D = $Camera


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	entity.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN
	entity.simulation.schedule = NetwSimulationHandle.SCHEDULE_TICK
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"rotation").broadcast() \
			.interpolate(NetwInterpolate.new().angle())


func _ready() -> void:
	camera.current = entity.is_controlled_locally


func _unhandled_input(event: InputEvent) -> void:
	if not entity.is_controlled_locally:
		return
	for action: StringName in pressed:
		if event.is_action(action):
			pressed[action] = event.is_action_pressed(action, true)
	if event is InputEventMouseMotion:
		rotate_y(-event.relative.x * 0.003)
	if event.is_action_pressed(&"grab"):
		var target := reach.get_collider() as PlayCube
		if target != null and target.grab(hand):
			held = target
	elif event.is_action_released(&"grab") and held != null:
		held.throw(velocity - hand.global_basis.z * THROW_SPEED)
		held = null


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	motion = Vector2(
		float(pressed[&"right"]) - float(pressed[&"left"]),
		float(pressed[&"back"]) - float(pressed[&"forward"]),
	).normalized()
	velocity = global_basis * Vector3(motion.x, 0.0, motion.y) * SPEED
	move_and_slide()
