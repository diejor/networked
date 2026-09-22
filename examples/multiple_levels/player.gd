extends CharacterBody2D

@export_custom(PROPERTY_HINT_NONE, "suffix:px/s") var speed: float = 64

var pressed := {
	&"move_left": false,
	&"move_right": false,
	&"move_up": false,
	&"move_down": false,
}

@onready var teleport: TPComponent = %TPComponent
@onready var entity := NetwEntity.of(self)

var game: MultipleLevelsSession


func _init() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	var declared := Netw.configure_entity(self)
	declared.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	declared.interpolation.visual_root = ^"Icon"
	declared.prediction.consume_buffer_ticks = 0

	Netw.configure_property(self, &"position").on_spawn() \
			.interpolate(NetwInterpolate.new().lerp())


func _ready() -> void:
	game = Netw.service(self, MultipleLevelsSession)
	Netw.configure_property(self, &"position", false) \
			.persisted(game.save_schema.column_ref(MultipleLevelsSession.WHERE))
	Netw.configure_persistence(self) \
			.database(MultipleLevelsSession.SAVES) \
			.schema(game.save_schema) \
			.record_id(account_name) \
			.interval(5.0)


func account_name() -> StringName:
	return entity.entity_id


func _unhandled_input(event: InputEvent) -> void:
	if not is_multiplayer_authority():
		return
	for action: StringName in pressed:
		if event.is_action(action):
			pressed[action] = event.is_action_pressed(action, true)


func _physics_process(_delta: float) -> void:
	if not is_multiplayer_authority():
		return
	if teleport.is_moving:
		velocity = Vector2.ZERO
		return
	var dir := Vector2(
		float(pressed[&"move_right"]) - float(pressed[&"move_left"]),
		float(pressed[&"move_down"]) - float(pressed[&"move_up"]),
	).normalized()
	velocity = dir * speed
	move_and_slide()
