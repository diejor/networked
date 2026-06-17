extends CharacterBody2D

## The player's movement speed (in pixels per second).
const MOTION_SPEED = 90.0

## The delay before which you can place a new bomb (in seconds).
const BOMB_RATE = 0.5

@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""

@onready var inputs: Node = $Inputs
@onready var player_entity := MultiplayerEntity.unwrap(self)
@onready var label: Label = %label

@onready var ctx := Netw.ctx(self)
@onready var clock := ctx.services.get_clock()
@onready var gamestate: BomberGamestate = \
		ctx.services.get_service(BomberGamestate)


func _ready() -> void:
	stunned = false


## The simulation contract, run by the server (authoritative), the owning client
## (prediction), and the owning client again during replay (is_fresh = false).
func _network_tick(input: Dictionary, delta: float, _tick: int, is_fresh: bool) -> void:
	if stunned:
		velocity = Vector2.ZERO
	else:
		velocity = input.get(&"motion", Vector2.ZERO) * MOTION_SPEED

	velocity *= clock.physics_factor
	move_and_slide()
	velocity /= clock.physics_factor

	if multiplayer.multiplayer_peer == null or multiplayer.is_server():
		last_bomb_time += delta
		if is_fresh and not stunned and input.get(&"bombing", false):
			_try_place_bomb()


func _try_place_bomb() -> void:
	if last_bomb_time < BOMB_RATE:
		return
	last_bomb_time = 0.0
	$"../../BombSpawner".spawn([position, player_entity.peer_id])


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
