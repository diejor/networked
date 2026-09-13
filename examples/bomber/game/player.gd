extends CharacterBody2D

const BOMB = preload("uid://3uxvvsya0q1t")
const GHOST_BOMB = preload("uid://qtc6lu84omhi")
const TILE_SIZE := 48.0
const BOMB_CELL_TOLERANCE := TILE_SIZE * 1.5

## The player's movement speed (in pixels per second).
const MOTION_SPEED = 90.0

## The delay before which you can place a new bomb (in seconds).
const BOMB_RATE = 0.5

const POSITION_LIMITS := Vector2(-500.0, 1500.0)
const POSITION_STEP := 5.0
const POSITION_EPSILON := 4.0
const VELOCITY_LIMIT := MOTION_SPEED

@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""

@onready var inputs: Node = $Inputs
@onready var label: Label = %label

@onready var entity := NetwEntity.of(self)
@onready var bomb_action := Netw.action(_place_bomb)
@onready var gamestate: BomberGamestate = \
		Netw.service(self, BomberGamestate)


func _init() -> void:
	entity = Netw.configure_entity(self)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	entity.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN
	entity.prediction.archetype = NetwPredict.ARCHETYPE_KINEMATIC
	entity.interpolation.visual_root = ^"sprite"
	entity.interpolation.predicted_mode = \
	NetwMultiplayer.PREDICTED_MODE_BRACKETED

	Netw.configure_property(self, &"position").state().masked().on_spawn() \
			.quantize(
				NetwQuantizeScalar.new() \
						.limits(POSITION_LIMITS.x, POSITION_LIMITS.y) \
						.step(POSITION_STEP),
			) \
			.epsilon(POSITION_EPSILON) \
			.teleport_at(TILE_SIZE) \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"velocity").state().masked() \
			.quantize(
				NetwQuantizeScalar.new().limits(-VELOCITY_LIMIT, VELOCITY_LIMIT),
			) \
			.derived().reconcile_only()


func _ready() -> void:
	stunned = false
	bomb_action.timing_mode = NetwAction.TIMING_TICK_ALIGNED_STATE_READY
	bomb_action.predict = func() -> Node:
		var ghost := GHOST_BOMB.instantiate()
		ghost.position = position
		add_sibling(ghost)
		return ghost


func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
	last_bomb_time += delta
	if is_fresh and entity.is_controlled_locally \
			and not stunned and inputs.bombing:
		if last_bomb_time < BOMB_RATE:
			return
		bomb_action.request(tick, position)
		if not multiplayer or not multiplayer.is_server():
			last_bomb_time = 0.0

	if stunned:
		velocity = Vector2.ZERO
	else:
		velocity = inputs.motion * MOTION_SPEED

	var factor: float = Netw.clock(self).monitor(
		NetwMultiplayer.CLOCK_MONITOR_PHYSICS_FACTOR,
	)
	velocity *= factor
	move_and_slide()
	velocity /= factor


# Validates and spawns a bomb on the server.
func _place_bomb(action_context: NetwActionContext, pos: Vector2) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	if last_bomb_time < BOMB_RATE:
		action_context.deny()
		return
	var past := Netw.sample(entity, action_context.view_tick)
	if not past.has_value(&"position"):
		action_context.deny()
		return
	var past_position := past.get_value(&"position") as Vector2
	if past_position.distance_to(pos) > BOMB_CELL_TOLERANCE:
		action_context.deny()
		return
	last_bomb_time = 0.0
	var real := BOMB.instantiate() as Area2D
	real.position = pos
	real.from_player = entity.peer_id
	action_context.bind(real)
	entity.scene.root.get_node(^"Bombs").add_child(real)


func _process(_delta: float) -> void:
	var new_anim := &"standing"
	if stunned:
		new_anim = &"stunned"
	elif velocity.y < 0:
		new_anim = &"walk_up"
	elif velocity.y > 0:
		new_anim = &"walk_down"
	elif velocity.x < 0:
		new_anim = &"walk_left"
	elif velocity.x > 0:
		new_anim = &"walk_right"

	if new_anim != current_anim:
		current_anim = new_anim
		$anim.play(current_anim)


@rpc("any_peer", "call_local", "reliable")
func set_player_name(value: String) -> void:
	label.text = value
	label.modulate = gamestate.get_player_color(value)
	$sprite.modulate = Color(0.5, 0.5, 0.5) + gamestate.get_player_color(value)


@rpc("any_peer", "call_local", "reliable")
func exploded(_by_who: int) -> void:
	if stunned:
		return

	stunned = true
	$anim.play(&"stunned")
