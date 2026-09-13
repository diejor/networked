extends CharacterBody2D

@export_custom(PROPERTY_HINT_NONE, "suffix:px/s") var speed: float = 64

@onready var input: MoveInputComponent = %InputComponent


func _init() -> void:
	var entity := Netw.configure_entity(self)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	entity.interpolation.visual_root = ^"Icon"
	entity.prediction.consume_buffer_ticks = 0

	Netw.configure_persistence(self) \
			.database(preload("res://examples/quick_start/quick_start_database.tres")) \
			.table(&"players")
	Netw.configure_property(self, &"position").persisted().on_spawn() \
			.interpolate(NetwInterpolate.new().lerp())


func _physics_process(_delta: float) -> void:
	if not is_multiplayer_authority():
		return
	var teleport := get_node_or_null("%TPComponent") as TPComponent
	if teleport and teleport.is_moving:
		velocity = Vector2.ZERO
		return
	var dir := input.get_vector2(
		input.move_left,
		input.move_right,
		input.move_up,
		input.move_down,
	)
	velocity = dir * speed
	move_and_slide()
