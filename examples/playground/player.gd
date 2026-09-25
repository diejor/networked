class_name PlayPlayer
extends RigidBody3D

const MASS := 1.0
const ATTRACT_MASS := 10.0
const ACCELERATION := 40.0
const ANGULAR_DAMP := 0.6
const HOVER_ANGULAR_DAMP := 9.0
const HOVER_HEIGHT := 1.5
const STIFFNESS := 150.0
const REPULSION := 3.0
const ATTRACTION := 20.0
const ATTRACTION_RADIUS := 1.0
const CAMERA_PITCH := 0.436
const CAMERA_DISTANCE := 8.0
const CAMERA_TARGET_HEIGHT := -0.6
const CAMERA_FOLLOW := 9.75

var color := Color.WHITE
var forward := Vector3.FORWARD
var material := StandardMaterial3D.new()
var held: Array[PlayCube] = []
var pressed := {
	&"left": false, &"right": false, &"forward": false, &"back": false,
	&"hover": false, &"attract": false,
}
var attracting := false
var hovering := false
var steer := Vector3.ZERO

var entity: NetwEntity

@onready var visual: MeshInstance3D = $Visual
@onready var nearby: Area3D = $Nearby
@onready var camera: Camera3D = $Camera


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	entity.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN
	entity.transfer = NetwEntity.TRANSFER_IMMEDIATE
	entity.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	entity.simulation.claim_on_contact = true
	entity.interpolation.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast().on_spawn() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"quaternion").broadcast().on_spawn()
	Netw.configure_property(self, &"linear_velocity").broadcast()
	Netw.configure_property(self, &"angular_velocity").broadcast()
	Netw.configure_property(self, &"attracting").broadcast().heartbeat(60)
	Netw.configure_property(self, &"hovering").broadcast()
	Netw.configure_property(self, &"steer").broadcast()


func _ready() -> void:
	material.albedo_color = color
	visual.material_override = material
	camera.current = entity.is_controlled_locally
	camera.global_position = visual.global_position - forward * CAMERA_DISTANCE


func _unhandled_input(event: InputEvent) -> void:
	if not entity.is_controlled_locally:
		return
	for action: StringName in pressed:
		if event.is_action(action):
			pressed[action] = event.is_action_pressed(action, true)
	if event.is_action_released(&"attract"):
		for cube in held:
			if is_instance_valid(cube):
				cube.let_go()
		held.clear()


func _process(delta: float) -> void:
	if not camera.current:
		return
	var direction := forward.rotated(forward.cross(Vector3.UP), -CAMERA_PITCH)
	var shown := visual.get_global_transform_interpolated().origin
	var target := shown + Vector3.UP * CAMERA_TARGET_HEIGHT
	camera.global_position = camera.global_position.lerp(
		target - direction * CAMERA_DISTANCE,
		1.0 - exp(-CAMERA_FOLLOW * delta),
	)
	camera.global_basis = Basis.looking_at(direction)


func _physics_process(_delta: float) -> void:
	if entity.is_controlled_locally:
		attracting = pressed[&"attract"]
		hovering = pressed[&"hover"]
		steer = steering()
	mass = MASS * (ATTRACT_MASS if attracting else 1.0)
	if attracting:
		attract()
	if hovering:
		angular_damp = HOVER_ANGULAR_DAMP
		hover(steer)
	else:
		angular_damp = ANGULAR_DAMP
		apply_central_force(steer * mass * ACCELERATION)


func steering() -> Vector3:
	var right := forward.cross(Vector3.UP)
	var direction := forward * (float(pressed[&"forward"]) - float(pressed[&"back"]))
	direction += right * (float(pressed[&"right"]) - float(pressed[&"left"]))
	return direction.normalized()


func hover(direction: Vector3) -> void:
	var lift := Vector3.UP * (HOVER_HEIGHT - global_position.y) * STIFFNESS
	apply_force(
		lift + direction * mass * ACCELERATION,
		global_basis * Vector3(0.0, 0.5, 0.0),
	)
	for cube in cubes_nearby():
		if entity.is_controlled_locally:
			cube.claim(NetwEntity.HOLD_YIELDABLE)
		if not drives(cube):
			continue
		var away := cube.global_position - global_position
		away.y = 0.0
		away = away.normalized()
		away.y = 1.0
		cube.apply_force(away.normalized() * REPULSION, Vector3(0.0, 0.025, 0.0))


func attract() -> void:
	for cube in cubes_nearby():
		var pull := global_position - cube.global_position
		if pull.length() >= ATTRACTION_RADIUS:
			continue
		if entity.is_controlled_locally:
			cube.claim(NetwEntity.HOLD_EXCLUSIVE)
			if cube.entity.is_controlled_locally and not held.has(cube):
				held.append(cube)
		if drives(cube):
			cube.apply_central_force(pull.normalized() * ATTRACTION)


func drives(cube: PlayCube) -> bool:
	if entity.is_controlled_locally:
		return cube.entity.is_controlled_locally
	return cube.entity.controller == entity.controller


func cubes_nearby() -> Array[PlayCube]:
	var found: Array[PlayCube] = []
	for body in nearby.get_overlapping_bodies():
		if body is PlayCube:
			found.append(body)
	return found
