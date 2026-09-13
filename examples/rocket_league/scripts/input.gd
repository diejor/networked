extends InputComponent

@export var motion := Vector2.ZERO
@export var jumping := false

var ai_enabled := false
var ai_motion := Vector2.ZERO
var ai_jumping := false

@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var steer_left: StringName = "left"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var steer_right: StringName = "right"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var accelerate: StringName = "forward"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var brake: StringName = "back"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var jump: StringName = "bounce"


func _init() -> void:
	var axis := NetwQuantizeScalar.new().bits(8).limits(-1.0, 1.0)
	Netw.configure_property(self, &"motion").input().quantize(axis)
	Netw.configure_property(self, &"jumping").input()


func _get_inputs() -> Array:
	return [steer_left, steer_right, accelerate, brake, jump]


func _gather() -> void:
	if ai_enabled:
		motion = ai_motion
		jumping = ai_jumping
	else:
		motion = get_vector2(steer_left, steer_right, brake, accelerate)
		jumping = is_down(jump)
