class_name NetLog
extends Object

enum Level { TRACE, DEBUG, INFO, WARN, ERROR, NONE }

static var current_level: int = Level.INFO
static var module_levels: Dictionary = {}  # String -> int

# Precomputed minimum across current_level and all module_levels.
# Allows TRACE/DEBUG calls to bail out before get_stack() when nothing is listening.
static var _min_active_level: int = Level.INFO

# Test isolation stack.
static var _settings_stack: Array = []

# Addon root path relative to res:// (e.g. "addons/networked"), set by plugin.gd.
# When set, module paths for scripts inside the addon are stored without this prefix
# so that saved overrides survive the addon directory being moved.
static var _addon_root: String = ""

# Guards lazy runtime initialization so it only runs once per game session.
static var _runtime_initialized: bool = false

const SETTING_ACTIVE_PROFILE = "networked/logging/active_profile"

## Called explicitly by plugin.gd (editor) or any runtime entry point.
## Pass addon_root (e.g. "res://addons/networked") so module paths are addon-relative.
static func initialize(addon_root: String = "") -> void:
	_addon_root = addon_root.replace("res://", "").trim_suffix("/")
	_runtime_initialized = true
	current_level = Level.INFO
	module_levels.clear()
	_load_active_profile()
	_recompute_min_level()

## Loads the active profile from ProjectSettings into the live module_levels table.
static func _load_active_profile() -> void:
	if not ProjectSettings.has_setting(SETTING_ACTIVE_PROFILE):
		return
	var raw: String = ProjectSettings.get_setting(SETTING_ACTIVE_PROFILE)
	if raw.is_empty():
		return

	var path := _fix_profile_path(raw)
	if path != raw:
		ProjectSettings.set_setting(SETTING_ACTIVE_PROFILE, path)
		ProjectSettings.save()

	if not ResourceLoader.exists(path):
		push_warning(
			"NetLog: Active profile not found: '%s'\n  → The resource may have been deleted or moved outside the editor.\n  → Clear or reassign it at Project Settings > %s" \
			% [path, SETTING_ACTIVE_PROFILE]
		)
		return

	var res = ResourceLoader.load(path)
	if res is NetLogSettings:
		current_level = res.global_level
		module_levels = res.module_overrides.duplicate()
	else:
		push_warning(
			"NetLog: '%s' is not a NetLogSettings resource.\n  → Reassign it at Project Settings > %s" \
			% [path, SETTING_ACTIVE_PROFILE]
		)

## Fixes a double-prefix written by an earlier version of the editor panel (uid://uid://...).
static func _fix_profile_path(path: String) -> String:
	if path.begins_with("uid://uid://"):
		return path.substr("uid://".length())
	return path

static func _recompute_min_level() -> void:
	_min_active_level = current_level
	for l: int in module_levels.values():
		if l < _min_active_level:
			_min_active_level = l

## Push a settings resource onto the isolation stack. Used by test hooks.
static func push_settings(settings: NetLogSettings) -> void:
	_settings_stack.push_back(settings)

## Pop the topmost settings from the isolation stack.
static func pop_settings() -> void:
	if not _settings_stack.is_empty():
		_settings_stack.pop_back()

## Returns the effective minimum log level for the given module path,
## walking up the dot-separated hierarchy until a match is found.
static func get_effective_level(module_path: String) -> int:
	if not _settings_stack.is_empty():
		var top: NetLogSettings = _settings_stack.back()
		var parts := module_path.split(".")
		while parts.size() > 0:
			var path := ".".join(parts)
			if top.module_overrides.has(path):
				return top.module_overrides[path]
			parts.remove_at(parts.size() - 1)
		return top.global_level

	if not module_path.is_empty():
		var parts := module_path.split(".")
		while parts.size() > 0:
			var path := ".".join(parts)
			if module_levels.has(path):
				return module_levels[path]
			parts.remove_at(parts.size() - 1)

	return current_level

static func trace(msg: Variant, args: Array = []) -> void:
	if Level.TRACE < _min_active_level and _settings_stack.is_empty(): return
	var ctx := _get_context()
	if Level.TRACE >= get_effective_level(ctx.module):
		_print("[TRACE]", msg, args, Level.TRACE, ctx.module, ctx.site)

static func debug(msg: Variant, args: Array = []) -> void:
	if Level.DEBUG < _min_active_level and _settings_stack.is_empty(): return
	var ctx := _get_context()
	if Level.DEBUG >= get_effective_level(ctx.module):
		_print("[DEBUG]", msg, args, Level.DEBUG, ctx.module, ctx.site)

static func info(msg: Variant, args: Array = []) -> void:
	if Level.INFO < _min_active_level and _settings_stack.is_empty(): return
	var ctx := _get_context()
	if Level.INFO >= get_effective_level(ctx.module):
		_print("[INFO]", msg, args, Level.INFO, ctx.module, ctx.site)

static func warn(msg: Variant, args: Array = []) -> void:
	if Level.WARN < _min_active_level and _settings_stack.is_empty(): return
	var ctx := _get_context()
	if Level.WARN >= get_effective_level(ctx.module):
		_print("[WARN]", msg, args, Level.WARN, ctx.module, ctx.site)

static func error(msg: Variant, args: Array = []) -> void:
	if Level.ERROR < _min_active_level and _settings_stack.is_empty(): return
	var ctx := _get_context()
	if Level.ERROR >= get_effective_level(ctx.module):
		_print("[ERROR]", msg, args, Level.ERROR, ctx.module, ctx.site)

# --- Internals ---

static func _get_context() -> Dictionary:
	var stack := get_stack()

	# Lazy initialization for game runtime: plugin.gd is editor-only, so the
	# first log call detects the addon root and loads the active profile.
	if not _runtime_initialized:
		_runtime_initialized = true
		for frame: Dictionary in stack:
			var src: String = frame.get("source", "")
			if src.ends_with("net_log.gd"):
				_addon_root = src.get_base_dir().get_base_dir() \
					.replace("res://", "").trim_suffix("/")
				break
		_load_active_profile()
		_recompute_min_level()

	for i in range(1, stack.size()):
		var frame: Dictionary = stack[i]
		var source: String = frame.source
		if source.ends_with("net_log.gd"):
			continue
		return {
			"module": _module_from_path(source),
			"site": "[%s:%d] " % [source.get_file(), frame.line]
		}
	return {"module": "", "site": ""}

## Converts a script path to a dot-separated module identifier.
## Scripts inside the addon are stored relative to the addon root so overrides
## survive the addon directory being moved.
## e.g. res://addons/networked/core/network_session.gd -> core.network_session
##      res://game/player.gd                           -> game.player
static func _module_from_path(path: String) -> String:
	var p := path.replace("res://", "").trim_suffix("/")
	if p.ends_with(".gd"):
		p = p.left(p.length() - 3)
	if not _addon_root.is_empty() and p.begins_with(_addon_root + "/"):
		p = p.substr(_addon_root.length() + 1)
	return p.replace("/", ".")

static func _print(prefix: String, msg: Variant, args: Array, level: int, module: String, site: String) -> void:
	var body: String = str(msg) % args if not args.is_empty() else str(msg)
	var header: String
	if not module.is_empty():
		# Drop the last component (filename) — it duplicates what the call site already shows.
		var parts := module.split(".")
		var display := ".".join(parts.slice(0, parts.size() - 1)) if parts.size() > 1 else module
		header = "%s {%s} %s" % [prefix, display, site]
	else:
		header = "%s %s" % [prefix, site]

	match level:
		Level.ERROR:
			push_error(header + body)
			print_rich("[color=red][b]%s[/b][/color]%s" % [header, body])
		Level.WARN:
			push_warning(header + body)
			print_rich("[color=yellow]%s[/color]%s" % [header, body])
		Level.TRACE:
			print_rich("[color=#555555]%s%s[/color]" % [header, body])
		Level.DEBUG:
			print_rich("[color=gray]%s[/color]%s" % [header, body])
		_:
			print_rich("[color=white]%s[/color]%s" % [header, body])
