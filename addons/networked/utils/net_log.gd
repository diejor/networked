## Static logging utility for the networked addon with per-module level overrides.
##
## Log calls resolve a per-module level by walking the dot-separated module hierarchy.
## At runtime the first log call lazily detects the addon root and loads the active
## [NetLogSettings] profile from [code]ProjectSettings[/code].
## [codeblock]
## NetLog.info("Player spawned: %s" % username)
## NetLog.warn("Connection attempt failed, retrying...")
## NetLog.error("Critical: lobby '%s' not found." % lobby_name)
## [/codeblock]
class_name NetLog
extends Object

## Ordered severity levels for filtering log output.
enum Level { INHERIT = -1, TRACE = 0, DEBUG = 1, INFO = 2, WARN = 3, ERROR = 4, NONE = 5 }

## Global minimum log level applied when no per-module override matches.
static var current_level: int = Level.INFO

## Per-module level overrides keyed by dot-separated module path (e.g. [code]"core.network_session"[/code]).
static var module_levels: Dictionary = {}

static var _min_active_level: int = Level.INFO
static var _effective_min_level: int = Level.INFO
static var _settings_stack: Array[NetLogSettings] = []
static var _addon_root: String = ""
static var _runtime_initialized: bool = false

const SETTING_ACTIVE_PROFILE = "networked/logging/active_profile"

## Initializes the logging system and loads the active profile from [code]ProjectSettings[/code].
##
## Call this explicitly from [code]plugin.gd[/code] (editor) or a runtime entry point.
## [param addon_root] — path such as [code]"res://addons/networked"[/code] — makes module
## paths relative to the addon root so saved overrides survive directory renames.
static func initialize(addon_root: String = "") -> void:
	_addon_root = addon_root.replace("res://", "").trim_suffix("/")
	_runtime_initialized = true
	current_level = Level.INFO
	module_levels.clear()
	_load_active_profile()
	_recompute_min_level()

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

## Fixes a double-prefix written by an earlier version of the editor panel ([code]uid://uid://...[/code]).
static func _fix_profile_path(path: String) -> String:
	if path.begins_with("uid://uid://"):
		return path.substr("uid://".length())
	return path

static func _recompute_min_level() -> void:
	_min_active_level = current_level
	for l: int in module_levels.values():
		if l != Level.INHERIT and l < _min_active_level:
			_min_active_level = l
	_effective_min_level = _stack_min_level() if not _settings_stack.is_empty() else _min_active_level

static func _stack_min_level() -> int:
	var m: int = _min_active_level
	for top: NetLogSettings in _settings_stack:
		if top.global_level != Level.INHERIT and top.global_level < m:
			m = top.global_level
		for l: int in top.module_overrides.values():
			if l != Level.INHERIT and l < m:
				m = l
	return m

static func is_level_active_for_module(level: int, module_path: String) -> bool:
	if level < _effective_min_level: 
		return false
	return level >= get_effective_level(module_path)

## Fast check to see if a level is active for a specific script path.
## Allows components to early-out before doing expensive string formatting.
static func is_level_active(level: int, script_path: String) -> bool:
	if level < _effective_min_level: 
		return false
	
	var module := _module_from_path(script_path)
	return level >= get_effective_level(module)

## Pushes a [NetLogSettings] resource onto the isolation stack.
##
## Settings pushed onto the stack cascade: queries check the top of the stack first,
## and if no explicit override (or INHERIT) is found, fall back down the stack until
## reaching the base profile settings.
static func push_settings(settings: NetLogSettings) -> void:
	_settings_stack.push_back(settings)
	_effective_min_level = _stack_min_level()

## Pops the topmost [NetLogSettings] from the isolation stack.
static func pop_settings() -> void:
	if not _settings_stack.is_empty():
		_settings_stack.pop_back()
	_effective_min_level = _stack_min_level() if not _settings_stack.is_empty() else _min_active_level

## Returns the effective log level for [param module_path].
##
## Walks up the dot-separated hierarchy (e.g. [code]"core.lobby.manager"[/code] →
## [code]"core.lobby"[/code] → [code]"core"[/code]) until a matching override is found.
## Cascades through pushed settings before falling back to the base profile.
static func get_effective_level(module_path: String) -> int:
	var parts := module_path.split(".")
	
	for i in range(_settings_stack.size() - 1, -1, -1):
		var top: NetLogSettings = _settings_stack[i]
		
		var temp_parts := parts.duplicate()
		while temp_parts.size() > 0:
			var path := ".".join(temp_parts)
			if top.module_overrides.has(path):
				var lvl: int = top.module_overrides[path]
				if lvl != Level.INHERIT:
					return lvl
			temp_parts.remove_at(temp_parts.size() - 1)
			
		if top.global_level != Level.INHERIT:
			return top.global_level

	if not module_path.is_empty():
		var temp_parts_base := parts.duplicate()
		while temp_parts_base.size() > 0:
			var path := ".".join(temp_parts_base)
			if module_levels.has(path):
				var lvl: int = module_levels[path]
				if lvl != Level.INHERIT:
					return lvl
			temp_parts_base.remove_at(temp_parts_base.size() - 1)

	return current_level

## Pushes a new configuration onto the stack using the LOGL string syntax.
## Example: [code]NetLog.push_setting_str("info,core.network=trace,components=none")[/code]
static func push_setting_str(logl_str: String) -> void:
	push_settings(parse_logl(logl_str))

## Parses a LOGL string into a [NetLogSettings] resource.
static func parse_logl(logl_str: String) -> NetLogSettings:
	var res := NetLogSettings.new()
	res.global_level = Level.INHERIT
	
	if logl_str.strip_edges().is_empty():
		return res
		
	var directives := logl_str.split(",", false)
	for d in directives:
		var d_str := d.strip_edges()
		if d_str.is_empty():
			continue
		
		var parts := d_str.split("=", false, 1)
		if parts.size() == 1:
			var lvl_str := parts[0].strip_edges().to_upper()
			if _string_to_level(lvl_str) != Level.INHERIT:
				res.global_level = _string_to_level(lvl_str)
		elif parts.size() == 2:
			var mod_path := parts[0].strip_edges()
			var lvl_str := parts[1].strip_edges().to_upper()
			res.module_overrides[mod_path] = _string_to_level(lvl_str)
			
	return res

## Serializes a [NetLogSettings] resource into a LOGL string.
static func to_logl(settings: NetLogSettings) -> String:
	var parts: Array = []
	if settings.global_level != Level.INHERIT:
		parts.append(_level_to_string(settings.global_level).to_lower())
		
	for mod_path: String in settings.module_overrides.keys():
		var lvl: int = settings.module_overrides[mod_path]
		if lvl != Level.INHERIT:
			parts.append("%s=%s" % [mod_path, _level_to_string(lvl).to_lower()])
			
	return ",".join(parts)

static func _string_to_level(s: String) -> int:
	match s:
		"TRACE": return Level.TRACE
		"DEBUG": return Level.DEBUG
		"INFO": return Level.INFO
		"WARN": return Level.WARN
		"ERROR": return Level.ERROR
		"NONE": return Level.NONE
		"INHERIT": return Level.INHERIT
		_: return Level.INHERIT

static func _level_to_string(l: int) -> String:
	match l:
		Level.TRACE: return "TRACE"
		Level.DEBUG: return "DEBUG"
		Level.INFO: return "INFO"
		Level.WARN: return "WARN"
		Level.ERROR: return "ERROR"
		Level.NONE: return "NONE"
		_: return "INHERIT"

## Dumps the current configuration state to the console.
static func dump_settings() -> void:
	print_rich("[color=cyan][b]--- NetLog Configuration Dump ---[/b][/color]")
	print_rich("[color=gray]Addon Root:[/color] %s" % (_addon_root if not _addon_root.is_empty() else "(empty)"))
	
	var base_settings := NetLogSettings.new()
	base_settings.global_level = current_level
	base_settings.module_overrides = module_levels.duplicate()
	var base_logl := to_logl(base_settings)
	print_rich("[color=gray]Base LOGL:[/color] [color=yellow]%s[/color] [color=gray](copied to clipboard)[/color]" % base_logl)
	DisplayServer.clipboard_set(base_logl)
	print_rich("[color=gray]Global Level:[/color] %s" % _level_to_string(current_level))
	
	if module_levels.is_empty():
		print_rich("[color=gray]Module Overrides: (none)[/color]")
	else:
		print_rich("[color=gray]Module Overrides:[/color]")
		for mod in module_levels:
			print_rich("  [color=yellow]%s[/color] = %s" % [mod, _level_to_string(module_levels[mod])])
			
	if not _settings_stack.is_empty():
		print_rich("[color=gray]Settings Stack (%d layers):[/color]" % _settings_stack.size())
		for i in range(_settings_stack.size() - 1, -1, -1):
			var settings: NetLogSettings = _settings_stack[i]
			print_rich("  [Layer %d] %s" % [i, to_logl(settings)])
			
	print_rich("[color=gray]Effective Min Level:[/color] %s" % _level_to_string(_effective_min_level))
	print_rich("[color=cyan][b]---------------------------------[/b][/color]")


## Logs a [code]TRACE[/code]-level message. Accepts optional [param args] for [code]%[/code]-style formatting.
static func trace(msg: Variant, args: Array = []) -> void:
	if Level.TRACE < _effective_min_level: return
	var ctx := _get_context()
	if Level.TRACE >= get_effective_level(ctx.module):
		_print("[TRACE]", msg, args, Level.TRACE, ctx.module, ctx.site)

## Logs a [code]DEBUG[/code]-level message. Accepts optional [param args] for [code]%[/code]-style formatting.
static func debug(msg: Variant, args: Array = []) -> void:
	if Level.DEBUG < _effective_min_level: return
	var ctx := _get_context()
	if Level.DEBUG >= get_effective_level(ctx.module):
		_print("[DEBUG]", msg, args, Level.DEBUG, ctx.module, ctx.site)

## Logs an [code]INFO[/code]-level message. Accepts optional [param args] for [code]%[/code]-style formatting.
static func info(msg: Variant, args: Array = []) -> void:
	if Level.INFO < _effective_min_level: return
	var ctx := _get_context()
	if Level.INFO >= get_effective_level(ctx.module):
		_print("[INFO]", msg, args, Level.INFO, ctx.module, ctx.site)

## Logs a [code]WARN[/code]-level message and calls [code]push_warning[/code].
## Accepts optional [param args] for [code]%[/code]-style formatting.
static func warn(msg: Variant, args: Array = []) -> void:
	if Level.WARN < _effective_min_level: return
	var ctx := _get_context()
	if Level.WARN >= get_effective_level(ctx.module):
		_print("[WARN]", msg, args, Level.WARN, ctx.module, ctx.site)

## Logs an [code]ERROR[/code]-level message and calls [code]push_error[/code].
## Accepts optional [param args] for [code]%[/code]-style formatting.
static func error(msg: Variant, args: Array = []) -> void:
	if Level.ERROR < _effective_min_level: return
	var ctx := _get_context()
	if Level.ERROR >= get_effective_level(ctx.module):
		_print("[ERROR]", msg, args, Level.ERROR, ctx.module, ctx.site)

static func _get_context() -> Dictionary:
	var stack := get_stack()

	# plugin.gd is editor-only; the first runtime log call detects the addon
	# root from the call stack and loads the active profile.
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
		if source.ends_with("net_log.gd") or source.ends_with("net_component.gd") or source.ends_with("tp_layer_api.gd"):
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
