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

@onready var ctx := Netw.of(self)
@onready var entity := NetwEntity.of(self)
@onready var bomb_action := ctx.lagcomp_action(_place_bomb)
@onready var gamestate: BomberGamestate = \
		ctx.get_service(BomberGamestate)


# Declares the server-authored state set off this script: position on a fixed
# world-grid step and velocity bit-packed to its speed envelope, the same
# quantization the wire carried when a synchronizer node owned the stream.
#
# The recovery marks say what each field means to a correction, in that field's
# own units. A game on the tick tier had never been declared this densely -- every
# finding in the prediction campaign came from the one solver game -- so this is
# also the second path the reachability report is answered on, and
# [TestBomberGameHarness] asserts its answers.
func _init() -> void:
	var entity := NetwEntity.resolve(self)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	entity.on_controller_disconnect = NetwEntity.DISCONNECT_DESPAWN

	var position_quantizer := NetwQuantizeFixed.new()
	position_quantizer.resolution_step = 5.0
	position_quantizer.min_limit = -500.0
	position_quantizer.max_limit = 1500.0
	# A tolerance in pixels and a tier distance in pixels: four is under a
	# quantizer step of slack, one tile is the distance past which the predicted
	# body is somewhere else entirely and easing it back would only be slower.
	#
	# The carry_step() is deliberately INERT and deliberately here. A forward
	# model needs the frame tier -- the tick tier re-anchors its own state record
	# to authority on every correction, so too little of the record is the
	# owner's own to replay a rule against -- and this body runs the tick tier.
	# Declaring one anyway is legal, is accepted, and is refused on first use,
	# which is exactly the pairing the reachability report exists to name before
	# a player feels it. This game is where inert_forward_model fires, and the
	# harness asks the report for it.
	Netw.configure_property(self, &"position").state().masked().on_spawn() \
			.quantize(position_quantizer).epsilon(4.0) \
			.teleport_at(TILE_SIZE) \
			.carry_step(_advance_position)
	var velocity_quantizer := NetwQuantizeBits.new()
	velocity_quantizer.min_limit = -90.0
	velocity_quantizer.max_limit = 90.0
	# The body recomputes velocity from the input every tick, so it is derived,
	# and a field the next step overwrites has no business deciding that a
	# correction is needed. The class is what says so, and it carries no
	# epsilon() because nothing compares the field for a tolerance to bound.
	Netw.configure_property(self, &"velocity").state().masked() \
			.quantize(velocity_quantizer) \
			.derived().reconcile_only()


# The forward model for position, declared under the tick tier where the engine
# refuses it. It is written to be correct rather than to be a stub: were this
# body on the frame tier, advancing an acknowledged position by the velocity the
# same record carries is what a recovery would need. It reads ctx.state rather
# than this node's velocity for the reason the engine enforces -- a rule is a
# function of the recorded transition alone, and reading the live world makes it
# disagree with the history it is replayed against.
func _advance_position(
		value: Vector2,
		ctx: NetwPredictCarryContext,
) -> Vector2:
	return value + (ctx.state.get(&"velocity", Vector2.ZERO) as Vector2) \
			* ctx.delta


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

	velocity *= ctx.clock.physics_factor
	move_and_slide()
	velocity /= ctx.clock.physics_factor


# Validates and spawns a bomb on the server.
func _place_bomb(action_context: NetwAction.Context, pos: Vector2) -> void:
	if not multiplayer or not multiplayer.is_server():
		return
	if last_bomb_time < BOMB_RATE:
		action_context.deny()
		return
	var past := ctx.lagcomp_sample(entity.rid, action_context.view_tick)
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
	NetwEntity.of(self).scene.level.get_node(^"Bombs").add_child(real)


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
