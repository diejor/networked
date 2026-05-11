extends CharacterBody2D

## The player's movement speed (in pixels per second).
const MOTION_SPEED = 90.0

## The delay before which you can place a new bomb (in seconds).
const BOMB_RATE = 0.5

@export var synced_position := Vector2()

@export var stunned: bool = false

var last_bomb_time := BOMB_RATE
var current_anim: String = ""

@onready var inputs: Node = $Inputs
@onready var player_spawner := SpawnerComponent.unwrap(self)
@onready var label: Label = %label

func _ready() -> void:
	stunned = false
	position = synced_position
	var peer_id := _get_player_peer_id()
	if peer_id != 0:
		$"Inputs/InputsSync".set_multiplayer_authority(peer_id)


func _physics_process(delta: float) -> void:
	var peer_id := _get_player_peer_id()
	if (
		multiplayer.multiplayer_peer == null
		or multiplayer.get_unique_id() == peer_id
	):
		# The represented client updates controls and replicates them.
		inputs.update()

	if multiplayer.multiplayer_peer == null or multiplayer.is_server():
		# The server updates the position replicated to clients.
		synced_position = position
		# And increase the bomb cooldown spawning one if the client wants to.
		last_bomb_time += delta
		if (
			not stunned
			and inputs.bombing
			and last_bomb_time >= BOMB_RATE
		):
			last_bomb_time = 0.0
			$"../../BombSpawner".spawn([position, peer_id])
	else:
		# The client simply updates the position to the last known one.
		position = synced_position

	if not stunned:
		# Everybody runs physics. Clients predict their next frame.
		velocity = inputs.motion * MOTION_SPEED
		move_and_slide()

	# Also update the animation based on the last known player input state.
	var new_anim := &"standing"

	if inputs.motion.y < 0:
		new_anim = &"walk_up"
	elif inputs.motion.y > 0:
		new_anim = &"walk_down"
	elif inputs.motion.x < 0:
		new_anim = &"walk_left"
	elif inputs.motion.x > 0:
		new_anim = &"walk_right"

	if stunned:
		new_anim = &"stunned"

	if new_anim != current_anim:
		current_anim = new_anim
		$anim.play(current_anim)


@rpc("call_local")
func set_player_name(value: String) -> void:
	label.text = value
	# Assign a random color to the player based on its name.
	var ctx := Netw.ctx(self)
	var gamestate: BomberGamestate = \
			ctx.services.get_service(BomberGamestate) if ctx.services else null
	if gamestate:
		label.modulate = gamestate.get_player_color(value)
		$sprite.modulate = Color(0.5, 0.5, 0.5) + gamestate.get_player_color(value)


@rpc("call_local")
func exploded(_by_who: int) -> void:
	if stunned:
		return

	stunned = true
	$anim.play(&"stunned")


func _get_player_peer_id() -> int:
	if player_spawner:
		return player_spawner.represented_peer_id
	return SpawnerComponent.parse_authority(name)
