## Wraps a panel [Control] in a titled container with a colored header bar.
##
## Owns the peer-context state (online / remote) and propagates it to the inner
## panel when the panel implements [DebugPanel]. Panels that do not implement
## [DebugPanel] are stored as plain [Control] and skipped for hooks.
##
## Double-clicking the title bar fires [member on_maximize_requested].
@tool
class_name PanelWrapper
extends PanelContainer

## Fired when the status icon (error/warning) is clicked.
signal status_pressed(level: int, summary: String)

## Called with [member adapter_key] when the title bar is double-clicked.
var on_maximize_requested: Callable

## Adapter key this wrapper is bound to - format "peer_key:panel_name".
var adapter_key: String

## The peer_key component of [member adapter_key], stored directly to avoid
## parsing the adapter key string at runtime.
var peer_key: String

## The inner panel control. May be any [Control] subclass.
var panel_control: Control

## Non-null when [member panel_control] extends [DebugPanel].
## Used for type-safe hook dispatch - no has_method checks needed.
var _debug_panel: DebugPanel

var _peer_color: Color
var _is_online: bool = true
var _title_label: Label
var _metric_label: Label
var _status_btn: Button
var _title_bar: HBoxContainer
var _title_panel: PanelContainer
var _title_style: StyleBoxFlat
var _outer_style: StyleBoxFlat
var _content_vbox: VBoxContainer

var _status_level: int = 0
var _status_summary: String = ""


func _init(
	p_key: String,
	p_peer_key: String,
	p_title: String,
	p_color: Color,
	p_panel: Control,
) -> void:
	adapter_key = p_key
	peer_key = p_peer_key
	panel_control = p_panel
	_debug_panel = p_panel as DebugPanel
	_peer_color = p_color
	size_flags_horizontal = Control.SIZE_EXPAND_FILL
	size_flags_vertical = Control.SIZE_EXPAND_FILL
	
	_content_vbox = VBoxContainer.new()
	_content_vbox.add_theme_constant_override("separation", 0)
	add_child(_content_vbox)
	
	_title_panel = PanelContainer.new()
	_title_panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_title_style = StyleBoxFlat.new()
	_title_style.bg_color = Color(p_color.r, p_color.g, p_color.b, 0.15)
	_title_style.corner_radius_top_left = 4
	_title_style.corner_radius_top_right = 4
	_title_panel.add_theme_stylebox_override("panel", _title_style)
	_title_panel.gui_input.connect(_on_title_bar_gui_input)
	_title_panel.mouse_filter = Control.MOUSE_FILTER_STOP
	_content_vbox.add_child(_title_panel)
	
	_title_bar = HBoxContainer.new()
	_title_bar.add_theme_constant_override("separation", 8)
	_title_bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_title_panel.add_child(_title_bar)
	
	_status_btn = Button.new()
	_status_btn.flat = true
	_status_btn.mouse_filter = Control.MOUSE_FILTER_STOP
	_status_btn.visible = false
	_status_btn.add_theme_stylebox_override("normal", StyleBoxEmpty.new())
	_status_btn.add_theme_stylebox_override("hover", StyleBoxEmpty.new())
	_status_btn.add_theme_stylebox_override("pressed", StyleBoxEmpty.new())
	_status_btn.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	_status_btn.pressed.connect(_on_status_btn_pressed)
	_title_bar.add_child(_status_btn)
	
	_title_label = Label.new()
	_title_label.text = p_title
	_title_label.add_theme_color_override("font_color", p_color)
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_title_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_title_bar.add_child(_title_label)
	
	_metric_label = Label.new()
	_metric_label.add_theme_color_override("font_color", p_color)
	_metric_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_title_bar.add_child(_metric_label)
	
	panel_control.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_content_vbox.add_child(panel_control)


func _notification(what: int) -> void:
	match what:
		NOTIFICATION_READY, NOTIFICATION_THEME_CHANGED:
			_rebuild_outer_style()
			_update_status_ui()


## Called by [method NetworkedDebuggerUI._add_wrapper_to_grid] once after the
## panel enters the scene tree. Sets remote and online context in a single call
## so panels receive both before any data arrives.
func init_peer_context(is_remote: bool, is_online: bool) -> void:
	if _debug_panel:
		_debug_panel.set_peer_remote(is_remote)
	if not is_online:
		set_online(false)


## Switches the wrapper between online and offline visual states.
## Offline: dims the title bar and border; panel content stays fully readable.
func set_online(online: bool) -> void:
	if _is_online == online:
		return
	_is_online = online
	var c := _title_style.bg_color
	c.a = 0.15 if online else 0.05
	_title_style.bg_color = c
	_title_label.modulate.a = 1.0 if online else 0.5
	_metric_label.modulate.a = 1.0 if online else 0.5
	_rebuild_outer_style()
	if _debug_panel:
		_debug_panel.set_peer_online(online)


## Adds a control to the header title bar (e.g. specialized toggle buttons).
func add_header_control(control: Control) -> void:
	if is_instance_valid(control):
		_title_bar.add_child(control)


## Updates the status icon and message displayed in the panel header.
func set_status(level: int, summary: String) -> void:
	_status_level = level
	_status_summary = summary
	_update_status_ui()


func update_live_metric(text: String) -> void:
	_metric_label.text = text


var _is_rebuilding_style: bool = false
func _rebuild_outer_style() -> void:
	if not is_inside_tree() or _is_rebuilding_style:
		return
	
	_is_rebuilding_style = true
	var border_alpha := 0.35 if _is_online else 0.12
	_outer_style = StyleBoxFlat.new()
	_outer_style.bg_color = get_theme_color("dark_color_1", "Editor")
	_outer_style.border_color = Color(
		_peer_color.r, _peer_color.g, _peer_color.b, border_alpha
	)
	_outer_style.set_border_width_all(1)
	_outer_style.set_corner_radius_all(4)
	_outer_style.content_margin_left   = 4
	_outer_style.content_margin_right  = 4
	_outer_style.content_margin_top    = 4
	_outer_style.content_margin_bottom = 4
	add_theme_stylebox_override("panel", _outer_style)
	_is_rebuilding_style = false


func _update_status_ui() -> void:
	if not is_inside_tree() or not _status_btn:
		return
	
	if _status_level == 0:
		_status_btn.visible = false
		return
	
	_status_btn.visible = true
	var icon_name := "NodeWarning" if _status_level == 1 else "StatusError"
	var color_name := "warning_color" if _status_level == 1 else "error_color"
	
	_status_btn.icon = get_theme_icon(icon_name, "EditorIcons")
	var icon_color := get_theme_color(color_name, "Editor")
	_status_btn.add_theme_color_override("icon_normal_color", icon_color)
	_status_btn.add_theme_color_override("icon_hover_color", icon_color)
	_status_btn.add_theme_color_override("icon_pressed_color", icon_color)
	
	var type_str := "Warning" if _status_level == 1 else "Error"
	_status_btn.tooltip_text = "%s in %s:\n%s" % [
		type_str, _title_label.text, _status_summary
	]


func _on_status_btn_pressed() -> void:
	status_pressed.emit(_status_level, _status_summary)


func _on_title_bar_gui_input(event: InputEvent) -> void:
	var mb := event as InputEventMouseButton
	if mb and mb.double_click and mb.button_index == MOUSE_BUTTON_LEFT:
		if on_maximize_requested.is_valid():
			on_maximize_requested.call(adapter_key)
