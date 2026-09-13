extends InputComponent

@export var steer := 0.0:
	set(value):
		steer = clampf(value, -1.0, 1.0)

@export var throttle := 0.0:
	set(value):
		throttle = clampf(value, -1.0, 1.0)


func _init() -> void:
	var axis := NetwQuantizeScalar.new()
	axis.bit_count = 16
	axis.min_limit = -1.0
	axis.max_limit = 1.0
	Netw.configure_property(self, &"steer").input().quantize(axis)
	Netw.configure_property(self, &"throttle").input().quantize(axis)

@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var steer_left: StringName = "left"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var steer_right: StringName = "right"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var accelerate: StringName = "forward"
@export_custom(PROPERTY_HINT_INPUT_NAME, &"input")
var brake: StringName = "back"


func _get_inputs() -> Array:
	return [steer_left, steer_right, accelerate, brake]


func _gather() -> void:
	steer = get_axis(steer_left, steer_right)
	throttle = get_axis(brake, accelerate)
