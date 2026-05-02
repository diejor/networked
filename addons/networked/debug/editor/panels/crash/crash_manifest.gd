## Crash Manifest panel - the Orchestrator Peak.
##
## Displays received [NetDebugManifest] entries as an interactive [Tree].
## Selecting a row calls back into [NetworkedDebuggerUI] to synchronize
## the Span Tracer, Matrices, and Components panels.
@tool
class_name PanelCrashManifest
extends DebugPanel

## Called when the user selects a manifest row.
## Signature: func(ctx: Dictionary) -> void
## ctx keys: cid, frame, player_name, tree_name, lobby_name
var on_context_selected: Callable

## Called when the Break on Manifest icon button is toggled.
## Signature: func(enabled: bool) -> void
var on_auto_break_changed: Callable

var _break_btn: CheckButton
var _tree: Tree
var _copy_btn: Button
var _clear_btn: Button

# Manifest entry dicts in insertion order (for copy/export).
var _entries: Array = []

# cid -> group TreeItem. Each unique CID gets a collapsible "Validation Cycle" header.
var _cid_groups: Dictionary[String, TreeItem] = {}

# "cid:trigger" -> top-level row TreeItem. Used for merging related errors.
var _top_rows: Dictionary[String, TreeItem] = {}

# "cid:trigger" -> "Intercepted Error" parent TreeItem.
var _error_parents: Dictionary[String, TreeItem] = {}

var _dbg: NetwHandle = Netw.dbg.handle(self)


func _ready() -> void:
	var toolbar := HBoxContainer.new()
	toolbar.add_theme_constant_override("separation", 4)
	add_child(toolbar)

	_copy_btn = Button.new()
	_copy_btn.text = "Copy"
	_copy_btn.disabled = true
	_copy_btn.pressed.connect(_on_copy)
	toolbar.add_child(_copy_btn)

	_clear_btn = Button.new()
	_clear_btn.text = "Clear"
	_clear_btn.pressed.connect(clear)
	toolbar.add_child(_clear_btn)

	_break_btn = CheckButton.new()
	_break_btn.text = "Break"
	_break_btn.tooltip_text = "Pause the game the moment a crash manifest arrives for this peer."
	_break_btn.mouse_filter = Control.MOUSE_FILTER_STOP
	_break_btn.toggled.connect(func(enabled: bool) -> void:
		if on_auto_break_changed.is_valid():
			on_auto_break_changed.call(enabled)
	)
	toolbar.add_child(_break_btn)

	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 3
	_tree.set_column_title(0, "Event / Detail")
	_tree.set_column_title(1, "Frame")
	_tree.set_column_title(2, "CID")
	_tree.column_titles_visible = true
	_tree.hide_root = true
	_tree.set_column_expand(0, true)
	_tree.set_column_expand(1, false)
	_tree.set_column_expand(2, false)
	_tree.set_column_custom_minimum_width(1, 70)
	_tree.set_column_custom_minimum_width(2, 140)
	_tree.item_selected.connect(_on_item_selected)
	add_child(_tree)

	# Placeholder until first manifest arrives.
	_tree.create_item()  # invisible root
	var placeholder := _tree.create_item(_tree.get_root())
	placeholder.set_text(0, "No crash manifest received yet.")
	placeholder.set_custom_color(0, Color(0.5, 0.5, 0.5))
	placeholder.set_selectable(0, false)
	placeholder.set_selectable(1, false)
	placeholder.set_selectable(2, false)


func clear() -> void:
	_entries.clear()
	_cid_groups.clear()
	_top_rows.clear()
	_error_parents.clear()
	_tree.clear()
	_tree.create_item()  # re-create invisible root

	var placeholder := _tree.create_item(_tree.get_root())

	placeholder.set_text(0, "No crash manifest received yet.")
	placeholder.set_custom_color(0, Color(0.5, 0.5, 0.5))
	placeholder.set_selectable(0, false)
	placeholder.set_selectable(1, false)
	placeholder.set_selectable(2, false)
	_copy_btn.disabled = true


var _is_remote: bool = false
func set_peer_remote(remote: bool) -> void:
	if _is_remote == remote:
		return
	_is_remote = remote
	clear()


## Populates the panel from [param buffer] all at once (called on checkbox toggle-on).
## Each entry is an already-formatted dict as produced by [CrashAdapter] (via [ManifestFormatter]).
func populate(buffer: Array) -> void:
	clear()
	for entry: Dictionary in buffer:
		push_entry(entry)


## Pushes a single new entry (called per [signal PanelDataAdapter.data_changed]).
func on_new_entry(entry: Variant) -> void:
	push_entry(entry as Dictionary)


## Add a formatted manifest entry (output of [ManifestFormatter.format]).
func push_entry(entry: Dictionary) -> void:
	var trigger: String = entry.get("trigger", "UNKNOWN")
	var cid: String = entry.get("cid", "N/A")
	var key := "%s:%s" % [cid, trigger]

	# Merge subsequent errors from the same validation cycle into the existing row.
	if key in _top_rows:
		var top: TreeItem = _top_rows[key]
		var existing_entry: Dictionary = top.get_metadata(0)
		var new_error: String = entry.get("error_text", "")
		if not new_error.is_empty():
			if not existing_entry.get("error_text", "").is_empty():
				existing_entry["error_text"] += "\n" + new_error
			else:
				existing_entry["error_text"] = new_error
			_append_error_lines(key, new_error)

			_dbg.debug("[UI] Merged subsequent error into existing row for CID: %s" % [cid])
			return
	# Remove placeholder on first real entry.
	if _entries.is_empty() and _tree.get_root():
		var child := _tree.get_root().get_first_child()
		while child:
			if "No crash manifest" in child.get_text(0):
				child.free()
				break
			child = child.get_next()

	_entries.append(entry)

	if not _tree.get_root():
		_tree.create_item()

	# --- CID group header ------------------------------------------------------
	if cid not in _cid_groups:
		var group := _tree.create_item(_tree.get_root())
		var cid_display := cid.substr(0, 24) + ("..." if cid.length() > 24 else "")
		group.set_text(0, "Validation Cycle  %s" % cid_display)
		group.set_custom_color(0, Color(0.55, 0.75, 1.0))
		group.set_selectable(0, false)
		group.set_selectable(1, false)
		group.set_selectable(2, false)
		_cid_groups[cid] = group

	var parent_group: TreeItem = _cid_groups[cid]

	# --- Top-level row ---------------------------------------------------------
	var top := _tree.create_item(parent_group)
	_top_rows[key] = top
	top.set_text(0, "! " + entry.get("label", "UNKNOWN"))
	top.set_text(1, str(entry.get("frame", 0)))
	var cid_short: String = entry.get("cid", "?")
	if cid_short.length() > 20:
		cid_short = cid_short.substr(0, 20) + "..."
	top.set_text(2, cid_short)
	top.set_custom_color(0, Color(1.0, 0.4, 0.4))
	top.set_custom_color(2, Color(0.4, 0.9, 1.0))
	top.set_metadata(0, entry)

	# --- CID Timeline ---------------------------------------------------------
	var timeline: Array = entry.get("cid_timeline", [])
	if not timeline.is_empty():
		var tl_row := _tree.create_item(top)
		tl_row.set_text(0, "Timeline: " + " <- ".join(timeline))
		tl_row.set_custom_color(0, Color(0.5, 0.8, 0.9))
		tl_row.set_selectable(0, false)
		tl_row.set_selectable(1, false)
		tl_row.set_selectable(2, false)

	# --- Network State --------------------------------------------------------
	var net: Dictionary = entry.get("network_state", {})
	if not net.is_empty():
		var ns_row := _tree.create_item(top)
		var side: String = "Server" if net.get("is_server", false) else "Client"
		ns_row.set_text(0, "Network: %s  peer_id=%d" % [side, net.get("peer_id", 0)])
		ns_row.set_selectable(0, false)
		ns_row.set_selectable(1, false)
		ns_row.set_selectable(2, false)

	# --- Error Text -----------------------------------------------------------
	var error_text: String = entry.get("error_text", "")
	if not error_text.is_empty():
		_append_error_lines(key, error_text)

	# --- Preflight Snapshot ---------------------------------------------------
	var preflight: Array = entry.get("preflight", [])
	if not preflight.is_empty():
		var pf_parent := _tree.create_item(top)
		pf_parent.set_text(0, "Preflight (%d node(s))" % preflight.size())
		pf_parent.set_custom_color(0, Color(0.8, 0.8, 0.5))
		pf_parent.set_selectable(0, false)
		pf_parent.set_selectable(1, false)
		pf_parent.set_selectable(2, false)
		for pf: Dictionary in preflight:
			var pf_row := _tree.create_item(pf_parent)
			pf_row.set_text(0, "  " + pf.get("label", "?"))
			pf_row.set_tooltip_text(0, pf.get("tooltip", ""))
			var node_path: String = pf.get("path", "")
			if not node_path.is_empty():
				pf_row.set_metadata(0, {"node_path": node_path, "source_entry": entry})
			# Color by broadcast vs. actual race
			if pf.get("broadcast", false):
				pf_row.set_custom_color(0, Color(0.6, 0.6, 0.6))
			else:
				pf_row.set_custom_color(0, Color(1.0, 0.5, 0.5))

	# --- Telemetry Slice ------------------------------------------------------
	var telemetry: Array = entry.get("telemetry", [])
	if not telemetry.is_empty():
		var telem_parent := _tree.create_item(top)
		telem_parent.set_text(0, "Telemetry (%d frames)" % telemetry.size())
		telem_parent.set_custom_color(0, Color(0.6, 0.75, 0.6))
		telem_parent.set_collapsed(true)
		telem_parent.set_selectable(0, false)
		telem_parent.set_selectable(1, false)
		telem_parent.set_selectable(2, false)
		for tl: Dictionary in telemetry:
			var tl_row := _tree.create_item(telem_parent)
			tl_row.set_text(0, "  " + tl.get("label", "?"))
			tl_row.set_tooltip_text(0, tl.get("tooltip", ""))
			tl_row.set_selectable(0, false)
			tl_row.set_selectable(1, false)
			tl_row.set_selectable(2, false)

	# --- Node Snapshot --------------------------------------------------------
	var snap: Dictionary = entry.get("node_snapshot", {})
	if not snap.is_empty():
		var snap_parent := _tree.create_item(top)
		snap_parent.set_text(0, "Node Snapshot  %s" % snap.get("node_name", "?"))
		snap_parent.set_custom_color(0, Color(0.7, 0.85, 0.7))
		snap_parent.set_collapsed(true)
		snap_parent.set_selectable(0, false)
		snap_parent.set_selectable(1, false)
		snap_parent.set_selectable(2, false)
		var sync_props: Dictionary = snap.get("sync_properties", {})
		for prop: String in sync_props:
			var prop_row := _tree.create_item(snap_parent)
			prop_row.set_text(0, "  %s = %s" % [prop, str(sync_props[prop])])
			prop_row.set_selectable(0, false)
			prop_row.set_selectable(1, false)
			prop_row.set_selectable(2, false)
		var debug_state: Dictionary = snap.get("debug_state", {})
		if not debug_state.is_empty():
			var ds_row := _tree.create_item(snap_parent)
			ds_row.set_text(0, "  debug_state: %s" % str(debug_state))
			ds_row.set_selectable(0, false)
			ds_row.set_selectable(1, false)
			ds_row.set_selectable(2, false)

	top.set_collapsed(false)
	parent_group.set_collapsed(false)
	_copy_btn.disabled = false
	call_deferred("_scroll_to_bottom")



func _append_error_lines(key: String, error_text: String) -> void:
	var top: TreeItem = _top_rows[key]
	if key not in _error_parents:
		var err_parent := _tree.create_item(top)
		err_parent.set_text(0, "Intercepted Error")
		err_parent.set_custom_color(0, Color(1.0, 0.65, 0.1))
		err_parent.set_selectable(0, false)
		err_parent.set_selectable(1, false)
		err_parent.set_selectable(2, false)
		_error_parents[key] = err_parent
	
	var parent: TreeItem = _error_parents[key]
	for line in error_text.split("\n"):
		if line.strip_edges().is_empty():
			continue
		var el := _tree.create_item(parent)
		el.set_text(0, "  " + line.strip_edges())
		el.set_custom_color(0, Color(1.0, 0.55, 0.2))
		el.set_selectable(0, false)
		el.set_selectable(1, false)
		el.set_selectable(2, false)


func _scroll_to_bottom() -> void:
	var root: TreeItem = _tree.get_root()
	if not root:
		return # The tree is empty
		
	# Traverse the tree to find the last visible item
	var last_item: TreeItem = root
	var next_item: TreeItem = last_item.get_next_visible()
	
	while next_item:
		last_item = next_item
		next_item = last_item.get_next_visible()
		
	# Scroll the tree to make the last item visible
	_tree.scroll_to_item(last_item)


func _on_item_selected() -> void:
	var item := _tree.get_selected()
	if not item:
		return
	# TreeItem.get_metadata(col) returns null if nothing was set.
	var meta: Variant = item.get_metadata(0)
	if meta == null:
		# Walk up to find the nearest ancestor that has metadata.
		var p := item.get_parent()
		while p and p != _tree.get_root():
			var pm: Variant = p.get_metadata(0)
			if pm != null:
				meta = pm
				break
			p = p.get_parent()
	if meta == null:
		return

	if meta is Dictionary:
		var ctx: Dictionary
		if meta.has("source_entry"):
			# This is a preflight node row.
			var src: Dictionary = meta["source_entry"]
			ctx = {
				"cid": src.get("cid", ""),
				"frame": src.get("frame", 0),
				"player_name": src.get("player_name", ""),
				"tree_name": src.get("network_state", {}).get("tree_name", ""),
				"node_path": meta.get("node_path", ""),
			}
		else:
			# Top-level entry row.
			ctx = {
				"cid": meta.get("cid", ""),
				"frame": meta.get("frame", 0),
				"player_name": meta.get("player_name", ""),
				"tree_name": meta.get("network_state", {}).get("tree_name", ""),
				"node_path": "",
			}
		if on_context_selected.is_valid():
			on_context_selected.call(ctx)


func _on_copy() -> void:
	if _entries.is_empty():
		return
	var lines: PackedStringArray = []
	for e: Dictionary in _entries:
		lines.append("=== %s  (cid: %s) ===" % [e.get("trigger", "UNKNOWN"), e.get("cid", "?")])
		lines.append("Timeline: %s" % " <- ".join(e.get("cid_timeline", [])))
		lines.append("Frame: %d  |  Scene: %s" % [e.get("frame", 0), e.get("active_scene", "?")])
		
		var net: Dictionary = e.get("network_state", {})
		lines.append("Network: tree=%s peer=%d server=%s" % [
			net.get("tree_name", "?"), net.get("peer_id", 0), str(net.get("is_server", false))])
		
		if not e.get("error_text", "").is_empty():
			lines.append("\n[Error Text]\n%s" % e["error_text"])
		
		var preflight: Array = e.get("preflight", [])
		if not preflight.is_empty():
			lines.append("\n[Preflight Snapshot]")
			for pf: Dictionary in preflight:
				lines.append("  %s  (auth=%s)" % [pf.get("label", "?"), str(pf.get("auth", "?"))])
		
		var snap: Dictionary = e.get("node_snapshot", {})
		if not snap.is_empty():
			lines.append("\n[Node Snapshot: %s]" % snap.get("node_name", "?"))
			lines.append("  Path: %s" % snap.get("node_path", "?"))
			lines.append("  Authority: %d" % snap.get("authority", 0))
			
			var props: Dictionary = snap.get("sync_properties", {})
			if not props.is_empty():
				lines.append("  Properties:")
				for k in props:
					lines.append("    %s = %s" % [k, str(props[k])])
			
			var ds: Dictionary = snap.get("debug_state", {})
			if not ds.is_empty():
				lines.append("  Debug State: %s" % str(ds))

		var telemetry: Array = e.get("telemetry", [])
		if not telemetry.is_empty():
			lines.append("\n[Telemetry Slice]")
			for tl in telemetry:
				lines.append("  %s" % str(tl.get("label", "?")))
		
		lines.append("\n" + "-".repeat(40) + "\n")
		
	DisplayServer.clipboard_set("\n".join(lines))
