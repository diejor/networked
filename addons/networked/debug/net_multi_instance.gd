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
var _debounce_timer: SceneTreeTimer


func _enter_tree() -> void:
	if not OS.has_feature("debug") or DisplayServer.get_name() == "headless" \
			or OS.has_feature("web"):
		return
	
	if not ProjectSettings.get_setting(SETTING_AUTO_TILE, true):
		return
	
	_determine_instance_id()
	
	Netw.dbg.tiling_requested.connect(_on_tiling_requested)
	
	# Initial layout.
	_apply_window_layout(true)


func _determine_instance_id() -> void:
	for i in range(1, 13):
		var server := TCPServer.new()
		if server.listen(49152 + i) == OK:
			_instance_lock = server
			instance_id = i
			return
	
	instance_id = 1


func _get_total_instances() -> int:
	var count := 0
	for i in range(1, 13):
		if i == instance_id:
			count += 1
			continue
		
		var server := TCPServer.new()
		if server.listen(49152 + i) != OK:
			count += 1
		else:
			server.stop()
	
	return count


func _on_tiling_requested() -> void:
	# Debounce rapid tiling requests from the editor or tree setup.
	if _debounce_timer:
		return
	
	_debounce_timer = get_tree().create_timer(0.1)
	_debounce_timer.timeout.connect(
		func() -> void:
			_debounce_timer = null
			_apply_window_layout()
	)


func _apply_window_layout(force: bool = false) -> void:
	var total_count := _get_total_instances()
	
	if not force and total_count == _last_seen_count:
		return
	
	_last_seen_count = total_count
	
	var screen := DisplayServer.window_get_current_screen()
	var rect := DisplayServer.screen_get_usable_rect(screen)
	
	# 1. Calculate Grid Dimensions
	var cols := ceili(sqrt(float(total_count)))
	var rows := ceili(float(total_count) / float(cols))
	
	# 2. Account for OS Window Borders
	# We need to know how big the title bar and borders are to tile perfectly.
	var win_id := get_window().get_window_id()
	var decorations := DisplayServer.window_get_size_with_decorations(win_id) - \
		DisplayServer.window_get_size(win_id)
	
	var base_w := float(ProjectSettings.get_setting(
		"display/window/size/viewport_width", 1152
	))
	var base_h := float(ProjectSettings.get_setting(
		"display/window/size/viewport_height", 648
	))
	
	# 3. Calculate Maximum Scaling
	var margin := 20.0
	var usable_w := float(rect.size.x) - (margin * 2.0)
	var usable_h := float(rect.size.y) - (margin * 2.0)
	
	# Real window footprint per tile: (content + decorations)
	var sw: float = usable_w / ((base_w * cols) + (decorations.x * cols))
	var sh: float = usable_h / ((base_h * rows) + (decorations.y * rows))
	
	var scale_factor: float = clampf(minf(sw, sh), 0.1, 0.9)
	var content_size := Vector2i(Vector2(base_w, base_h) * scale_factor)
	var real_tile_size := content_size + decorations
	
	# 4. Calculate Centering Offset
	var grid_size := Vector2i(real_tile_size.x * cols, real_tile_size.y * rows)
	var center_offset := (Vector2i(rect.size) - grid_size) / 2
	
	# 5. Position in Grid
	var col := (instance_id - 1) % cols
	var row := (instance_id - 1) / cols
	
	var x := rect.position.x + center_offset.x + (col * real_tile_size.x)
	var y := rect.position.y + center_offset.y + (row * real_tile_size.y)
	
	# 6. Apply Identity & Identity
	var app_name: String = ProjectSettings.get_setting(
		"application/config/name", "Networked Game"
	)
	var rid := "?"
	if get_parent() and "reporter_id" in get_parent():
		rid = get_parent().reporter_id
	
	var new_title := "[ID:%d] [RID:%s] %s" % [instance_id, rid, app_name]
	
	Netw.dbg.trace("Tiling ID:%d. Total:%d. Pos:(%d,%d) Size:(%d,%d)" % \
		[instance_id, total_count, x, y, content_size.x, content_size.y])
	
	var apply_logic := func() -> void:
		var win := get_window()
		if not is_instance_valid(win):
			return
		
		if Engine.has_method("is_embedded_in_editor") and \
				Engine.is_embedded_in_editor():
			Netw.dbg.warn("Tiling blocked: 'Embed Game' is active. " + \
				"Disable it in Editor or disable 'auto_tile_instances' " + \
				"in Project Settings.", func(m: String) -> void: push_warning(m))
			return
		
		DisplayServer.window_set_title(new_title, win_id)
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED, win_id)
		DisplayServer.window_set_size(content_size, win_id)
		DisplayServer.window_set_position(Vector2i(x, y), win_id)
		
		# Persistence pass
		get_tree().create_timer(0.15).timeout.connect(
			func() -> void:
				if is_instance_valid(win):
					DisplayServer.window_set_position(Vector2i(x, y), win_id)
		)
		
		DisplayServer.window_set_flag(
			DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, true, win_id
		)
		DisplayServer.window_move_to_foreground(win_id)
		
		get_tree().create_timer(0.5).timeout.connect(
			func() -> void:
				if is_instance_valid(win):
					DisplayServer.window_set_flag(
						DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP, false, win_id
					)
		)
	
	apply_logic.call_deferred()
