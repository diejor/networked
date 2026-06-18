extends CharacterBody2D

const BOMB = preload("uid://3uxvvsya0q1t")
const GHOST_BOMB = preload("res://examples/bomber/game/ghost_bomb.tscn")
const TILE_SIZE := 48.0
const BOMB_CELL_TOLERANCE := TILE_SIZE * 1.5

## The player's movement speed (in pixels per second).
const MOTION_SPEED = 90.0

## The delay before which you can place a new bomb (in seconds).
const BOMB_RATE = 0.5

@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""
var _pending_bomb_cell := Vector2i.ZERO

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
	bomb_action.predict = _predict_bomb


## The simulation contract, run by the server (authoritative), the owning client
## (prediction), and the owning client again during replay (is_fresh = false).
## Input is on the live [code]inputs[/code] node, applied by the framework.
func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
	last_bomb_time += delta
	if is_fresh and entity.is_controlled_locally and not stunned and inputs.bombing:
		_try_place_bomb(tick)

	if stunned:
		velocity = Vector2.ZERO
	else:
		velocity = inputs.motion * MOTION_SPEED

	velocity *= clock.physics_factor
	move_and_slide()
	velocity /= clock.physics_factor


func _try_place_bomb(tick: int) -> void:
	if last_bomb_time < BOMB_RATE:
		return
	_pending_bomb_cell = _cell_under_position(position)
	bomb_action.request(tick, _pending_bomb_cell)
	last_bomb_time = 0.0


func _predict_bomb() -> Node:
	var ghost := GHOST_BOMB.instantiate()
	ghost.position = _cell_to_world(_pending_bomb_cell)
	add_sibling(ghost)
	return ghost


func _place_bomb(action_context: NetwAction.Context, cell: Vector2i) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	if last_bomb_time < BOMB_RATE:
		action_context.deny()
		return
	var past := lag.sample(entity, action_context.view_tick)
	if not past.has_value(&"position"):
		action_context.deny()
		return
	if not _cell_reachable_from(past.get_value(&"position"), cell):
		action_context.deny()
		return
	last_bomb_time = 0.0
	var real := BOMB.instantiate() as Area2D
	real.position = _cell_to_world(cell)
	real.from_player = entity.peer_id
	action_context.bind(real)
	$"../../Bombs".add_child(real)


func _cell_under_position(pos: Vector2) -> Vector2i:
	return Vector2i(
		int(round(pos.x / TILE_SIZE - 0.5)),
		int(round(pos.y / TILE_SIZE - 0.5)),
	)


func _cell_to_world(cell: Vector2i) -> Vector2:
	return Vector2(
		float(cell.x) * TILE_SIZE + TILE_SIZE * 0.5,
		float(cell.y) * TILE_SIZE + TILE_SIZE * 0.5,
	)


func _cell_reachable_from(pos: Vector2, cell: Vector2i) -> bool:
	return pos.distance_to(_cell_to_world(cell)) <= BOMB_CELL_TOLERANCE


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
