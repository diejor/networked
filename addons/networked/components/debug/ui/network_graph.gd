@tool
class_name NetworkGraph
extends Control

## The [NetworkClock] to monitor. If null, tries to find one on the multiplayer API.
@export var clock: NetworkClock

## Colors for different metrics.
@export var rtt_color: Color = Color.CYAN
@export var jitter_color: Color = Color.YELLOW
@export var offset_color: Color = Color.GREEN
@export var stable_color: Color = Color.GREEN
@export var unstable_color: Color = Color.RED

## How many samples to store in the graph.
@export var sample_count: int = 120

var _rtt_samples: Array[float] = []
var _jitter_samples: Array[float] = []
var _offset_samples: Array[float] = []

var _max_rtt: float = 0.1
var _max_jitter: float = 0.05
var _max_offset: float = 10.0

func _ready() -> void:
	if not clock:
		clock = NetworkClock.for_node(self)
	custom_minimum_size = Vector2(200, 100)


func _process(_delta: float) -> void:
	if Engine.is_editor_hint():
		queue_redraw()
		return
		
	if not clock:
		clock = NetworkClock.for_node(self)
		return

	_record_samples()
	queue_redraw()


func _record_samples() -> void:
	_rtt_samples.append(clock.rtt_avg)
	_jitter_samples.append(clock.rtt_jitter)
	_offset_samples.append(float(clock.display_offset))
	
	if _rtt_samples.size() > sample_count:
		_rtt_samples.pop_front()
		_jitter_samples.pop_front()
		_offset_samples.pop_front()
		
	_max_rtt = 0.1
	for s in _rtt_samples: _max_rtt = maxf(_max_rtt, s)
	
	_max_jitter = 0.05
	for s in _jitter_samples: _max_jitter = maxf(_max_jitter, s)
	
	_max_offset = 10.0
	for s in _offset_samples: _max_offset = maxf(_max_offset, s)


func _draw() -> void:
	if not clock:
		draw_string(ThemeDB.fallback_font, Vector2(10, 20), "No NetworkClock found.")
		return
		
	var rect := get_rect()
	rect.position = Vector2.ZERO
	
	# Background
	draw_rect(rect, Color(0, 0, 0, 0.5))
	
	# Stability Indicator
	var stability_color := stable_color if clock.is_stable else unstable_color
	draw_circle(Vector2(rect.size.x - 15, 15), 5, stability_color)
	
	if _rtt_samples.is_empty():
		return
		
	# Draw Graphs
	_draw_line_graph(_rtt_samples, _max_rtt, rtt_color)
	_draw_line_graph(_jitter_samples, _max_jitter, jitter_color)
	_draw_line_graph(_offset_samples, _max_offset, offset_color)
	
	# Labels
	var font := ThemeDB.fallback_font
	var font_size := ThemeDB.fallback_font_size
	
	draw_string(font, Vector2(5, 15), "RTT: %dms" % (clock.rtt_avg * 1000), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, rtt_color)
	draw_string(font, Vector2(5, 30), "Jitter: %dms" % (clock.rtt_jitter * 1000), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, jitter_color)
	draw_string(font, Vector2(5, 45), "Offset: %d" % clock.display_offset, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, offset_color)


func _draw_line_graph(samples: Array[float], max_val: float, color: Color) -> void:
	if samples.size() < 2:
		return
		
	var points := PackedVector2Array()
	var step := size.x / float(sample_count - 1)
	
	for i in range(samples.size()):
		var x := i * step
		var y := size.y - (samples[i] / max_val) * size.y
		points.append(Vector2(x, y))
		
	draw_polyline(points, color, 1.0, true)


func _on_configured() -> void:
	if multiplayer.is_server():
		queue_free()
