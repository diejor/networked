extends CharacterBody2D

const BOMB = preload("uid://3uxvvsya0q1t")
const GHOST_BOMB = preload("uid://qtc6lu84omhi")
const TILE_SIZE := 48.0
const BOMB_CELL_TOLERANCE := TILE_SIZE * 1.5

## The player's movement speed (in pixels per second).
const MOTION_SPEED = 90.0

## The delay before which you can place a new bomb (in seconds).
const BOMB_RATE = 0.5

@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""

@onready var inputs: Node = $Inputs
@onready var label: Label = %label

@onready var ctx := Netw.ctx(self)
@onready var clock := ctx.services.get_clock()
@onready var lag := ctx.lag_compensation
@onready var entity := ctx.entity
@onready var bomb_action := lag.action(_place_bomb)
@onready var gamestate: BomberGamestate = \
		ctx.services.get_service(BomberGamestate)


func _ready() -> void:
	stunned = false
	bomb_action.timing_mode = \
	NetwAction.TimingMode.TICK_ALIGNED_STATE_READY
	bomb_action.predict = func() -> Node:
		var ghost := GHOST_BOMB.instantiate()
		ghost.position = position
		add_sibling(ghost)
		return ghost


## The simulation contract, run by the server (authoritative), the owning client
## (prediction), and the owning client again during replay (is_fresh = false).
## Input is on the live [code]inputs[/code] node, applied by the framework.
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

	velocity *= clock.physics_factor
	move_and_slide()
	velocity /= clock.physics_factor


# Validates and spawns a bomb on the server.
func _place_bomb(action_context: NetwAction.Context, pos: Vector2) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	if last_bomb_time < BOMB_RATE:
		action_context.deny()
		return
	var past := lag.sample(entity, action_context.view_tick)
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
	$"../../Bombs".add_child(real)


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
	# Assign a random color to the player based on its name.
	label.modulate = gamestate.get_player_color(value)
	$sprite.modulate = Color(0.5, 0.5, 0.5) + gamestate.get_player_color(value)


@rpc("any_peer", "call_local", "reliable")
func exploded(_by_who: int) -> void:
	if stunned:
		return

	stunned = true
	$anim.play(&"stunned")
