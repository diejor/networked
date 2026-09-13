class_name RocketBall
extends RigidBody3D

const STARTING_POSITION := Vector3(0.0, 2.381, 0.0)
const CONFETTI := preload("res://examples/rocket_league/scenes/confetti.tscn")

var goal_team := -1

@onready var clock: NetwClockHandle = Netw.clock(self)
@onready var entity := NetwEntity.of(self)
@onready var state := PhysicsServer3D.body_get_direct_state(get_rid())
@onready var level: Node = entity.scene.root
@onready var game: RocketGame = level.get_node(^"game|0")
@onready var goals := level.get_node(^"field").find_children("*", "Area3D", false)

var pose: Transform3D:
	get:
		return state.transform if state else transform
	set(value):
		if state:
			state.transform = value
		else:
			transform = value

var ball_position: Vector3:
	get:
		return pose.origin
	set(value):
		var next := pose
		next.origin = value
		pose = next

var ball_rotation: Quaternion:
	get:
		return pose.basis.get_rotation_quaternion()
	set(value):
		var next := pose
		next.basis = Basis(value.normalized())
		pose = next

var ball_linear_velocity: Vector3:
	get:
		return state.linear_velocity
	set(value):
		state.linear_velocity = value

var ball_angular_velocity: Vector3:
	get:
		return state.angular_velocity
	set(value):
		state.angular_velocity = value

var ball_sleeping: bool:
	get:
		return state.sleeping
	set(value):
		state.sleeping = value


func _init() -> void:
	Netw.configure_property(self, &"ball_position").state().masked().causal() \
			.on_spawn().teleport_at(1.5) \
			.quantize(NetwQuantizeScalar.new().bits(19).limits(-128.0, 128.0)) \
			.interpolate(
				NetwInterpolate.new().lerp().snap_at(3.0) \
						.project_by(&"ball_linear_velocity").to(&"position"),
			)
	Netw.configure_property(self, &"ball_rotation").state().masked().causal() \
			.on_spawn() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"ball_linear_velocity").state().masked() \
			.causal().teleport_only() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-128.0, 128.0))
	Netw.configure_property(self, &"ball_angular_velocity").state().masked() \
			.causal().teleport_only() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-64.0, 64.0))
	Netw.configure_property(self, &"ball_sleeping").state().masked().causal()
	Netw.configure_property(self, &"goal_team").state().masked().causal() \
			.quantize(NetwQuantizeScalar.new().bits(2).limits(-1.0, 1.0))

	Netw.configure_rpc(announce_goal).authority().reliable()
	var e := NetwEntity.ensure(self)
	e.prediction.archetype = NetwPredict.ARCHETYPE_SOLVER_BODY
	e.prediction.schedule = RocketJoltStepper.schedule()
	e.prediction.recovery_policy = NetwPredict.RECOVERY_POLICY_REBASE_REPLAY
	e.prediction.sensors[&"goal"] = sample_goal
	e.prediction.witness_contacts = sample_contacts
	e.interpolation.visual_root = ^"MeshInstance3D"


func _ready() -> void:
	set_multiplayer_authority(1, true)
	contact_monitor = true
	max_contacts_reported = 8
	state.transform = transform


func _process(_delta: float) -> void:
	visible = clock.tick >= game.kickoff_tick


func _network_tick(_delta: float, tick: int, _is_fresh: bool) -> void:
	goal_team = entity.prediction.sensor(&"goal", -1)

	if not multiplayer.is_server():
		return

	if tick < game.kickoff_tick:
		reset()
		return

	if goal_team >= 0:
		game.rule_goal(goal_team, tick)
		Netw.rpc(announce_goal, goal_team)


func reset() -> void:
	pose = Transform3D(Basis.IDENTITY, STARTING_POSITION)
	state.linear_velocity = Vector3.ZERO
	state.angular_velocity = Vector3.ZERO


func announce_goal(_team: int) -> void:
	var confetti_instance := CONFETTI.instantiate() as Node3D
	get_tree().root.add_child(confetti_instance)
	confetti_instance.global_position = global_position


func sample_goal() -> int:
	for goal: Area3D in goals:
		if goal.overlaps_body(self):
			return int(String(goal.name).right(1))
	return -1


func sample_contacts() -> Dictionary:
	return {
		colliders = get_colliding_bodies(),
		sleeping = ball_sleeping,
		collision_layer = collision_layer,
		collision_mask = collision_mask,
	}
