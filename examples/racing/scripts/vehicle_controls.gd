extends InputComponent
## Controller-authored input set for a racing [Vehicle]: steer and throttle,
## bit-packed floats gathered every tick on the controlling client and applied
## on the server and during prediction replay before each network tick.

# Probe-only switch for measuring the codec's contribution to divergence.
static var probe_quantize_inputs := true

@export var steer := 0.0:
	set(value):
		steer = clampf(value, -1.0, 1.0)

@export var throttle := 0.0:
	set(value):
		throttle = clampf(value, -1.0, 1.0)


func _init() -> void:
	var axis_quantizer := NetwQuantizeBits.new()
	axis_quantizer.bit_count = 16
	axis_quantizer.min_limit = -1.0
	axis_quantizer.max_limit = 1.0
	var quantizers: Array = [axis_quantizer] if probe_quantize_inputs else [null]
	Netw.configure_property(self, &"steer").input().quantize(quantizers)
	Netw.configure_property(self, &"throttle").input().quantize(quantizers)

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
