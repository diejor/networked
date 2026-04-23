## Automatically tiles and titles debug instances of the game.
##
## This node is instantiated and added as a child to the
## [NetworkedDebugReporter] autoload during startup. It claims a local TCP port
## to determine its sequential instance ID, then positions the OS window
## accordingly to prevent overlapping.
extends Node
class_name NetMultiInstance

const SETTING_AUTO_TILE = "networked/debug/auto_tile_instances"

var instance_id: int = 1

var _instance_lock: TCPServer
var _last_seen_count: int = 0


func _enter_tree() -> void:
	if not OS.has_feature("debug") or DisplayServer.get_name() == "headless" \
			or OS.has_feature("web"):
		return
		
	var args := OS.get_cmdline_args()
	for arg in args:
		if arg == "--editor" or arg == "-e":
			return
	
	if not ProjectSettings.get_setting(SETTING_AUTO_TILE, true):
		return
		
	_determine_instance_id()
	
	# Initial layout.
	_apply_window_layout()
	
	# "Settle" the swarm: Re-calculate the grid as other instances boot up.
	# This ensures all windows eventually agree on the same 'total count'
	# and scale appropriately.
	for delay in [0.2, 0.5, 1.0, 2.0]:
		get_tree().create_timer(delay).timeout.connect(_apply_window_layout)


func _determine_instance_id() -> void:
	for i in range(1, 13):
		var server := TCPServer.new()
		if server.listen(49152 + i) == OK:
			_instance_lock = server
			instance_id = i
			return
			
	instance_id = 1


## Probes the port range to see how many total instances are currently running.
func _get_total_instances() -> int:
	var count := 0
	for i in range(1, 13):
		if i == instance_id:
			count += 1
			continue
			
		var server := TCPServer.new()
		# Port is busy -> another instance is holding the lock.
		if server.listen(49152 + i) != OK:
			count += 1
	return count


func _apply_window_layout() -> void:
	var total_count := _get_total_instances()
	
	# Optimization: Don't jump the window if the instance count hasn't changed.
	if total_count == _last_seen_count:
		return
	_last_seen_count = total_count
	
	var screen := DisplayServer.window_get_current_screen()
	var rect := DisplayServer.screen_get_usable_rect(screen)
	
	# 1. Calculate Optimal Grid
	var cols := ceili(sqrt(float(total_count)))
	var rows := ceili(float(total_count) / float(cols))
	
	# 2. Calculate Maximum Afforded Scaling
	var base_w := float(ProjectSettings.get_setting(
		"display/window/size/viewport_width", 1152
	))
	var base_h := float(ProjectSettings.get_setting(
		"display/window/size/viewport_height", 648
	))
	
	var margin_x := 120.0
	var margin_y := 100.0
	var padding := 40.0
	
	var usable_w := float(rect.size.x) - (margin_x * 2.0)
	var usable_h := float(rect.size.y) - (margin_y * 2.0)
	
	var scale_w: float = (usable_w - (padding * (cols - 1))) / (base_w * cols)
	var scale_h: float = (usable_h - (padding * (rows - 1))) / (base_h * rows)
	
	var scale_factor: float = clampf(minf(scale_w, scale_h), 0.1, 0.9)
	
	var new_size := Vector2i(Vector2(base_w, base_h) * scale_factor)
	DisplayServer.window_set_size(new_size)
	
	# 3. Position in Grid
	var col := (instance_id - 1) % cols
	var row := (instance_id - 1) / cols
	
	var x := int(float(rect.position.x) + margin_x + \
		(float(col) * (float(new_size.x) + padding)))
	var y := int(float(rect.position.y) + margin_y + \
		(float(row) * (float(new_size.y) + padding + 40.0)))
	
	DisplayServer.window_set_position(Vector2i(x, y))
	
	# 4. Identity & Windows Focus Force
	var app_name: String = ProjectSettings.get_setting(
		"application/config/name", "Networked Game"
	)
	var rid: String = "?"
	if get_parent() and "reporter_id" in get_parent():
		rid = get_parent().reporter_id
		
	DisplayServer.window_set_title("[RID: %s] %s" % [rid, app_name])
	
	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, true)
	DisplayServer.window_move_to_foreground()
	DisplayServer.window_request_attention()
	
	get_tree().create_timer(0.1).timeout.connect(func():
		DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, false)
	)
