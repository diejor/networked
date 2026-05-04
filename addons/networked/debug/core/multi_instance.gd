## Automatically tiles debug instances of the game.
##
## This node is added as a child to the [NetworkedDebugReporter] autoload
## during startup.

extends Node
class_name NetMultiInstance

## Project setting path for enabling/disabling automatic window tiling.
const SETTING_AUTO_TILE = "networked/debug/auto_tile_instances"


## The sequential ID of this debug instance (1-indexed).
var instance_id: int = 1


var _instance_lock: TCPServer
var _last_seen_count: int = 0
var _debounce_timer: SceneTreeTimer
var _dbg: NetwHandle = Netw.dbg.handle(self)


func _enter_tree() -> void:
	if not OS.has_feature("debug") or DisplayServer.get_name() == "headless" \
			or OS.has_feature("web"):
		return
	
	if not ProjectSettings.get_setting(SETTING_AUTO_TILE, true):
		return
	
	_determine_instance_id()
	
	Netw.dbg.tiling_requested.connect(_on_tiling_requested)
	
	_apply_window_layout(true)
	
	var timer := Timer.new()
	timer.wait_time = 2.0
	timer.autostart = true
	timer.timeout.connect(_enforce_state)
	add_child(timer)


func _determine_instance_id() -> void:
	var args := OS.get_cmdline_args()
	for i in range(args.size()):
		if args[i] == "--instance" and i + 1 < args.size():
			instance_id = args[i + 1].to_int()
			_dbg.trace("Multi-Instance: Identified via --instance as ID %d" % \
				instance_id)
			return
			
	for i in range(1, 13):
		var server := TCPServer.new()
		if server.listen(49152 + i) == OK:
			_instance_lock = server
			instance_id = i
			_dbg.trace("Multi-Instance: Identified via port-lock as ID %d" % \
				instance_id)
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
	if _debounce_timer:
		return
	
	var self_ref := weakref(self)
	_debounce_timer = get_tree().create_timer(0.1)
	_debounce_timer.timeout.connect(
		func() -> void:
			var inst = self_ref.get_ref()
			if inst:
				inst._debounce_timer = null
				inst._apply_window_layout()
	)


func _apply_window_layout(force: bool = false) -> void:
	var total_count := _get_total_instances()
	
	if not force and total_count == _last_seen_count:
		_enforce_state()
		return
	
	_last_seen_count = total_count
	
	var screen := DisplayServer.window_get_current_screen()
	var rect := DisplayServer.screen_get_usable_rect(screen)
	
	var cols := ceili(sqrt(float(total_count)))
	var rows := ceili(float(total_count) / float(cols))
	
	var win_id := get_window().get_window_id()
	var decorations := DisplayServer.window_get_size_with_decorations(win_id) - \
		DisplayServer.window_get_size(win_id)
	
	var base_w := float(ProjectSettings.get_setting(
		"display/window/size/viewport_width", 1152
	))
	var base_h := float(ProjectSettings.get_setting(
		"display/window/size/viewport_height", 648
	))
	
	var outer_margin := 100.0
	var inner_margin := 10.0
	
	# Calculate usable dimensions by subtracting outer and inner margins.
	var usable_w := float(rect.size.x) - (outer_margin * 2.0) - \
		(inner_margin * (cols - 1))
	var usable_h := float(rect.size.y) - (outer_margin * 2.0) - \
		(inner_margin * (rows - 1))
	
	var sw: float = usable_w / ((base_w * cols) + (decorations.x * cols))
	var sh: float = usable_h / ((base_h * rows) + (decorations.y * rows))
	
	var scale_factor: float = clampf(minf(sw, sh), 0.1, 1.0)
	var content_size := Vector2i(Vector2(base_w, base_h) * scale_factor)
	var real_tile_size := content_size + decorations
	
	# Calculate total grid size including inner margins.
	var grid_w := (real_tile_size.x * cols) + (inner_margin * (cols - 1))
	var grid_h := (real_tile_size.y * rows) + (inner_margin * (rows - 1))
	var grid_size := Vector2i(grid_w, grid_h)
	var center_offset := (Vector2i(rect.size) - grid_size) / 2
	
	var col := (instance_id - 1) % cols
	var row := (instance_id - 1) / cols
	
	var x := rect.position.x + center_offset.x + \
		(col * (real_tile_size.x + inner_margin))
	var y := rect.position.y + center_offset.y + \
		(row * (real_tile_size.y + inner_margin))
	
	_dbg.trace("Tiling ID:%d. Total:%d. Pos:(%d,%d) Size:(%d,%d)" % \
		[instance_id, total_count, x, y, content_size.x, content_size.y])
	
	var apply_logic := func() -> void:
		var win := get_window()
		if not is_instance_valid(win):
			return
		
		if Engine.has_method("is_embedded_in_editor") and \
				Engine.is_embedded_in_editor():
			return
		
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED, win_id)
		DisplayServer.window_set_size(content_size, win_id)
		DisplayServer.window_set_position(Vector2i(x, y), win_id)
		
		_enforce_state()
		
		var win_ref := weakref(win)
		get_tree().create_timer(0.2).timeout.connect(
			func() -> void:
				var w = win_ref.get_ref()
				if is_instance_valid(w):
					DisplayServer.window_set_position(Vector2i(x, y), win_id)
		)
	
	apply_logic.call_deferred()


func _enforce_state() -> void:
	var win := get_window()
	if not is_instance_valid(win):
		return
	
	var win_id := win.get_window_id()
	var on_top := DisplayServer.WINDOW_FLAG_ALWAYS_ON_TOP
	if not DisplayServer.window_get_flag(on_top, win_id):
		DisplayServer.window_set_flag(on_top, true, win_id)
