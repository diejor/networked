## Main "Networked" tab in the editor debugger.
##
## Hosts the Session Bar and the four sub-panels. All incoming game messages
## are routed here via [method on_message]. Ring buffers retain recent history
## so context is preserved when switching trees in the Session Bar dropdown.
@tool
class_name NetworkedDebuggerUI
extends VBoxContainer

const RING_SIZE := 50

# Set by the plugin after construction.
var plugin: NetworkedDebuggerPlugin
var session_id: int

# Whether the Freeze toggle is active.
var _freeze: bool = false

# Known trees: tree_name → {is_server, backend_class, online}
var _trees: Dictionary[String, Dictionary] = {}

# Currently selected tree name ("" means first available).
var _selected_tree: String = ""

# Ring buffers keyed by tree_name.
var _clock_samples: Dictionary[String, Array] = {}
var _lobby_snapshots: Dictionary[String, Dictionary] = {}
var _component_heartbeats: Dictionary[String, Dictionary] = {}
var _component_events: Array = []  # flat ring, last RING_SIZE

# Panels.
var _panel_matrices: PanelMatrices
var _panel_components: PanelComponents
var _panel_log: PanelLogBridge
var _panel_clock: PanelClock

# Session Bar widgets.
var _tree_selector: OptionButton
var _lamp: ColorRect
var _status_label: Label
var _freeze_btn: CheckButton


func _ready() -> void:
	custom_minimum_size.y = 250
	_build_session_bar()
	_build_tabs()


func _build_session_bar() -> void:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 8)
	add_child(bar)

	var lbl := Label.new()
	lbl.text = "Tree:"
	bar.add_child(lbl)

	_tree_selector = OptionButton.new()
	_tree_selector.custom_minimum_size.x = 180
	_tree_selector.item_selected.connect(_on_tree_selected)
	bar.add_child(_tree_selector)

	_lamp = ColorRect.new()
	_lamp.custom_minimum_size = Vector2(12, 12)
	_lamp.color = Color.GRAY
	bar.add_child(_lamp)

	_status_label = Label.new()
	_status_label.text = "offline"
	bar.add_child(_status_label)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(spacer)

	_freeze_btn = CheckButton.new()
	_freeze_btn.text = "Freeze"
	_freeze_btn.toggled.connect(func(v: bool) -> void: _freeze = v)
	bar.add_child(_freeze_btn)

	var export_btn := Button.new()
	export_btn.text = "Export State"
	export_btn.pressed.connect(_on_export_state)
	bar.add_child(export_btn)


func _build_tabs() -> void:
	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(tabs)

	_panel_matrices = PanelMatrices.new()
	_panel_matrices.name = "Matrices"
	_panel_matrices.send_to_game = func(msg: String, data: Array) -> void:
		if plugin:
			plugin.send_to_game(session_id, msg, data)
	tabs.add_child(_panel_matrices)

	_panel_components = PanelComponents.new()
	_panel_components.name = "Components"
	tabs.add_child(_panel_components)

	_panel_log = PanelLogBridge.new()
	_panel_log.name = "Log Bridge"
	tabs.add_child(_panel_log)

	_panel_clock = PanelClock.new()
	_panel_clock.name = "Clock"
	tabs.add_child(_panel_clock)


# ─── Message Dispatch ─────────────────────────────────────────────────────────

func on_message(message: String, data: Array) -> void:
	if _freeze or data.is_empty():
		return
	var d: Dictionary = data[0] if data[0] is Dictionary else {}

	match message:
		"networked:session_registered":   _on_session_registered(d)
		"networked:session_unregistered": _on_session_unregistered(d)
		"networked:peer_connected":       _on_peer_event(d, true)
		"networked:peer_disconnected":    _on_peer_event(d, false)
		"networked:clock_sample":         _on_clock_sample(d)
		"networked:lobby_snapshot":       _on_lobby_snapshot(d)
		"networked:lobby_event":          _on_lobby_event(d)
		"networked:component_heartbeat":  _on_component_heartbeat(d)
		"networked:component_event":      _on_component_event(d)
		"networked:replication_snapshot": _on_replication_snapshot(d)


func _on_session_registered(d: Dictionary) -> void:
	var name: String = d.get("tree_name", "")
	if name.is_empty():
		return
	_trees[name] = {
		"is_server": d.get("is_server", false),
		"backend_class": d.get("backend_class", ""),
		"online": true,
	}
	_refresh_tree_selector()


func _on_session_unregistered(d: Dictionary) -> void:
	var name: String = d.get("tree_name", "")
	if name in _trees:
		_trees[name]["online"] = false
	_refresh_tree_selector()


func _on_peer_event(d: Dictionary, _connected: bool) -> void:
	_update_status(d.get("tree_name", ""))


func _on_clock_sample(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if not tn in _clock_samples:
		_clock_samples[tn] = []
	var buf: Array = _clock_samples[tn]
	buf.append(d)
	if buf.size() > RING_SIZE:
		buf.pop_front()
	if _matches_selected(tn):
		_panel_clock.push_sample(d)


func _on_lobby_snapshot(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	_lobby_snapshots[tn] = d
	if _matches_selected(tn):
		_panel_matrices.update_visibility_matrix(d)


func _on_lobby_event(_d: Dictionary) -> void:
	pass  # Snapshots are polled at 2 Hz; events only invalidate cache if needed.


func _on_component_heartbeat(d: Dictionary) -> void:
	var tn: String = d.get("tree_name", "")
	if not tn in _component_heartbeats:
		_component_heartbeats[tn] = {}
	_component_heartbeats[tn][d.get("player_name", "")] = d
	if _matches_selected(tn):
		_panel_components.update_player(d)


func _on_component_event(d: Dictionary) -> void:
	_component_events.append(d)
	if _component_events.size() > RING_SIZE:
		_component_events.pop_front()
	if _matches_selected(d.get("tree_name", "")):
		_panel_log.push_event(d)


func _on_replication_snapshot(d: Dictionary) -> void:
	if _matches_selected(d.get("tree_name", "")):
		_panel_matrices.update_replication_matrix(d)


# ─── Session Bar Logic ────────────────────────────────────────────────────────

func _refresh_tree_selector() -> void:
	var prev := _tree_selector.get_item_text(_tree_selector.selected) if _tree_selector.selected >= 0 else ""
	_tree_selector.clear()
	var idx := 0
	var restore_idx := 0
	for tn: String in _trees:
		var info: Dictionary = _trees[tn]
		var prefix := "[S] " if info.get("is_server", false) else "[C] "
		var suffix := "" if info.get("online", false) else " (offline)"
		_tree_selector.add_item(prefix + tn + suffix)
		if tn == prev:
			restore_idx = idx
		idx += 1
	if _tree_selector.item_count > 0:
		_tree_selector.select(restore_idx)
		_on_tree_selected(restore_idx)


func _on_tree_selected(idx: int) -> void:
	if idx < 0 or idx >= _tree_selector.item_count:
		return
	# Extract tree name from label (strip prefix and suffix).
	var raw := _tree_selector.get_item_text(idx)
	_selected_tree = raw.substr(4).split(" (")[0]  # strip "[S] " or "[C] " and " (offline)"
	_update_status(_selected_tree)
	_repopulate_panels()


func _update_status(tree_name: String) -> void:
	if tree_name != _selected_tree:
		return
	var info: Dictionary = _trees.get(tree_name, {})
	var online: bool = info.get("online", false)
	_lamp.color = Color.GREEN if online else Color.RED
	_status_label.text = "online" if online else "offline"


func _repopulate_panels() -> void:
	_panel_clock.clear()
	_panel_components.clear()
	_panel_log.clear()
	_panel_matrices.clear()

	if _selected_tree in _clock_samples:
		for s in _clock_samples[_selected_tree]:
			_panel_clock.push_sample(s)

	if _selected_tree in _lobby_snapshots:
		_panel_matrices.update_visibility_matrix(_lobby_snapshots[_selected_tree])

	if _selected_tree in _component_heartbeats:
		for d in _component_heartbeats[_selected_tree].values():
			_panel_components.update_player(d)

	for ev in _component_events:
		if ev.get("tree_name", "") == _selected_tree:
			_panel_log.push_event(ev)


func _matches_selected(tree_name: String) -> bool:
	return _selected_tree.is_empty() or tree_name == _selected_tree


# ─── Export State ─────────────────────────────────────────────────────────────

func _on_export_state() -> void:
	var state := {
		"trees": _trees,
		"selected_tree": _selected_tree,
		"clock_samples": _clock_samples,
		"lobby_snapshots": _lobby_snapshots,
		"component_heartbeats": _component_heartbeats,
		"component_events": _component_events,
	}
	DisplayServer.clipboard_set(JSON.stringify(state, "\t"))
	print("[Networked Debugger] State exported to clipboard.")
