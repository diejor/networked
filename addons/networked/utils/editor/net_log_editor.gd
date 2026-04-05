@tool
extends Control

# UI nodes
var _tree: Tree
var _picker: EditorResourcePicker
var _settings: NetLogSettings

# Filesystem cache — built once per editor session, invalidated by Refresh.
var _module_cache: Array = []  # Array of _Entry dicts (see _scan_dir)
var _cache_valid: bool = false

const LEVELS = ["TRACE", "DEBUG", "INFO", "WARN", "ERROR", "NONE", "INHERIT"]
const ROOT_PATH = "res://"
const IGNORE_DIRS = [".godot", ".git", ".jj"]

# --- Setup ---

func _enter_tree() -> void:
	if not _tree:
		_build_ui()

	var active_path: String = ProjectSettings.get_setting(NetLog.SETTING_ACTIVE_PROFILE, "")
	if active_path.is_empty():
		return

	# Sanitize before touching ResourceLoader — a malformed path causes a C++ error.
	active_path = NetLog._fix_profile_path(active_path)

	if not ResourceLoader.exists(active_path):
		push_warning(
			"NetLog: Active profile no longer exists: '%s'\n  → Select a new profile in the NetLog panel." % active_path
		)
		ProjectSettings.set_setting(NetLog.SETTING_ACTIVE_PROFILE, "")
		ProjectSettings.save()
		return

	var res = ResourceLoader.load(active_path)
	if not res is NetLogSettings:
		push_warning(
			"NetLog: '%s' is not a NetLogSettings resource.\n  → Select a new profile in the NetLog panel." % active_path
		)
		ProjectSettings.set_setting(NetLog.SETTING_ACTIVE_PROFILE, "")
		ProjectSettings.save()
		return

	_picker.edited_resource = res
	_on_resource_changed(res)

func _build_ui() -> void:
	var vb := VBoxContainer.new()
	vb.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(vb)

	# Top bar: profile picker + refresh
	var hb := HBoxContainer.new()
	vb.add_child(hb)

	var label := Label.new()
	label.text = "Active Profile:"
	hb.add_child(label)

	_picker = EditorResourcePicker.new()
	_picker.base_type = "NetLogSettings"
	_picker.custom_minimum_size.x = 250
	_picker.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_picker.resource_changed.connect(_on_resource_changed)
	hb.add_child(_picker)

	var refresh_btn := Button.new()
	refresh_btn.text = "Refresh"
	refresh_btn.tooltip_text = "Re-scan the filesystem for new scripts"
	refresh_btn.pressed.connect(_on_refresh_pressed)
	hb.add_child(refresh_btn)

	# Module tree
	_tree = Tree.new()
	_tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.columns = 2
	_tree.set_column_title(0, "Module")
	_tree.set_column_title(1, "Log Level")
	_tree.column_titles_visible = true
	_tree.item_edited.connect(_on_item_edited)
	vb.add_child(_tree)

# --- Profile management ---

func _on_resource_changed(res: Resource) -> void:
	if res is NetLogSettings:
		_settings = res
		if not _settings.resource_path.is_empty():
			var uid_int := ResourceLoader.get_resource_uid(_settings.resource_path)
			var profile_ref: String = (
				ResourceUID.id_to_text(uid_int)
				if uid_int != ResourceUID.INVALID_ID
				else _settings.resource_path
			)
			ProjectSettings.set_setting(NetLog.SETTING_ACTIVE_PROFILE, profile_ref)
			ProjectSettings.save()
		NetLog.initialize()
		_refresh_tree()
	else:
		_settings = null
		ProjectSettings.set_setting(NetLog.SETTING_ACTIVE_PROFILE, "")
		ProjectSettings.save()
		_tree.clear()

func _on_refresh_pressed() -> void:
	_cache_valid = false
	_refresh_tree()

# --- Tree population ---

func _refresh_tree() -> void:
	if not _settings:
		return
	_tree.clear()

	var cache := _get_module_cache()
	_prune_stale_overrides(cache)

	var root := _tree.create_item()
	var profile_label := _settings.resource_path if not _settings.resource_path.is_empty() else "Unsaved"
	root.set_text(0, "Profile: %s" % profile_label)

	# Global level row
	var global_item := _tree.create_item(root)
	global_item.set_text(0, "Global Level")
	_setup_level_cell(global_item, _settings.global_level, false)

	# Module tree
	var project_root := _tree.create_item(root)
	project_root.set_text(0, "res://")
	project_root.set_selectable(0, false)
	project_root.set_selectable(1, false)

	for entry: Dictionary in cache:
		_add_tree_entry(entry, project_root)

func _add_tree_entry(entry: Dictionary, parent: TreeItem) -> void:
	var item := _tree.create_item(parent)
	item.set_text(0, entry.name + ("/" if entry.is_dir else ""))
	var level: int = _settings.module_overrides.get(entry.module_path, -1)
	_setup_level_cell(item, level, true)
	item.set_metadata(0, entry.module_path)
	for child: Dictionary in entry.children:
		_add_tree_entry(child, item)

func _setup_level_cell(item: TreeItem, current_level: int, can_inherit: bool) -> void:
	item.set_cell_mode(1, TreeItem.CELL_MODE_RANGE)
	var opts: Array = LEVELS if can_inherit else LEVELS.slice(0, LEVELS.size() - 1)
	item.set_text(1, ",".join(opts))
	item.set_range(1, LEVELS.size() - 1 if current_level == -1 else current_level)
	item.set_editable(1, true)

# --- Edit handler ---

func _on_item_edited() -> void:
	if not _settings:
		return
	var item := _tree.get_edited()
	if _tree.get_edited_column() != 1:
		return

	var val := int(item.get_range(1))
	var level_name: String = LEVELS[val]

	# Global level item is the first child of root
	if item.get_parent() == _tree.get_root() and item.get_index() == 0:
		_settings.global_level = val
		NetLog.current_level = val
		NetLog._recompute_min_level()
	else:
		var mod_path = item.get_metadata(0)
		if mod_path:
			if level_name == "INHERIT":
				_settings.module_overrides.erase(mod_path)
				NetLog.module_levels.erase(mod_path)
			else:
				_settings.module_overrides[mod_path] = val
				NetLog.module_levels[mod_path] = val
			NetLog._recompute_min_level()

	if not _settings.resource_path.is_empty():
		ResourceSaver.save(_settings, _settings.resource_path)

# --- Filesystem cache ---

func _get_module_cache() -> Array:
	if not _cache_valid:
		_module_cache = _scan_dir(ROOT_PATH)
		_cache_valid = true
	return _module_cache

## Recursively scans a directory and returns a list of entry dicts.
## Each entry: {name: String, module_path: String, is_dir: bool, children: Array}
## Dirs with no .gd files anywhere in their subtree are omitted.
func _scan_dir(path: String) -> Array:
	var dir := DirAccess.open(path)
	if not dir:
		return []

	var subdirs: Array = []
	var files: Array = []

	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		if entry != "." and entry != "..":
			if dir.current_is_dir():
				if not entry in IGNORE_DIRS:
					subdirs.append(entry)
			elif entry.ends_with(".gd"):
				files.append(entry)
		entry = dir.get_next()

	subdirs.sort()
	files.sort()

	var result: Array = []

	for d: String in subdirs:
		var full_path := path + d + "/"
		var children := _scan_dir(full_path)
		if children.is_empty():
			continue  # skip dirs with no GD files in the subtree
		result.append({
			"name": d,
			"module_path": _to_module_path(full_path),
			"is_dir": true,
			"children": children
		})

	for f: String in files:
		result.append({
			"name": f,
			"module_path": _to_module_path(path + f),
			"is_dir": false,
			"children": []
		})

	return result

func _to_module_path(path: String) -> String:
	var p := path.replace(ROOT_PATH, "").trim_suffix("/")
	if p.ends_with(".gd"):
		p = p.left(p.length() - 3)
	var addon_root := NetLog._addon_root
	if not addon_root.is_empty() and p.begins_with(addon_root + "/"):
		p = p.substr(addon_root.length() + 1)
	return p.replace("/", ".")

## Removes module_overrides entries whose paths no longer exist in the filesystem.
## Called after every cache build so stale keys from renamed/deleted scripts are cleaned up.
func _prune_stale_overrides(cache: Array) -> void:
	if not _settings or _settings.module_overrides.is_empty():
		return

	var valid_paths := _collect_module_paths(cache)
	var stale: Array = []
	for path: String in _settings.module_overrides.keys():
		if not valid_paths.has(path):
			stale.append(path)

	if stale.is_empty():
		return

	for path: String in stale:
		_settings.module_overrides.erase(path)
		NetLog.module_levels.erase(path)

	push_warning("NetLog: Pruned %d stale override(s) from '%s': %s" % [
		stale.size(), _settings.resource_path, ", ".join(stale)
	])

	NetLog._recompute_min_level()

	if not _settings.resource_path.is_empty():
		ResourceSaver.save(_settings, _settings.resource_path)

## Flattens the cache into a set of all known module paths (dirs and files).
func _collect_module_paths(entries: Array) -> Dictionary:
	var result := {}
	for entry: Dictionary in entries:
		result[entry.module_path] = true
		if not entry.children.is_empty():
			result.merge(_collect_module_paths(entry.children))
	return result
