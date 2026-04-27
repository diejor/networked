## Static logging utility for the networked addon with per-module level overrides.
##
## Log calls resolve a per-module level by walking the dot-separated module hierarchy.
## At runtime the first log call lazily detects the addon root and loads the active
## [NetwLogSettings] profile from [code]ProjectSettings[/code].
## [codeblock]
## Netw.dbg.info(self, "Player spawned: %s", [username])
## Netw.dbg.warn(self, "Connection attempt failed, retrying...")
## [/codeblock]
class_name NetwLog
extends Object

## Ordered severity levels for filtering log output.
enum Level {
	INHERIT = -1,
	TRACE = 0,
	DEBUG = 1,
	INFO = 2,
	WARN = 3,
	ERROR = 4,
	NONE = 5
}

## Global minimum log level applied when no per-module override matches.
static var current_level: int = Level.NONE

## Per-module level overrides keyed by dot-separated module path.
## Example: [code]"core.network_session"[/code].
static var module_levels: Dictionary = {}

static var _min_active_level: int = Level.NONE
static var _effective_min_level: int = Level.NONE
static var _effective_global_level: int = Level.NONE
static var _settings_stack: Array[NetwLogSettings] = []
static var _addon_root: String = ""
static var _runtime_initialized: bool = false
static var _is_debug: bool = OS.is_debug_build()

const SETTING_ACTIVE_PROFILE = "networked/logging/active_profile"


## Initializes the logging system and loads the active profile.
##
## Call this explicitly from [code]plugin.gd[/code] (editor) or a runtime entry
## point. [param addon_root] — path such as [code]"res://addons/networked"[/code]
## — makes module paths relative to the addon root so saved overrides survive
## directory renames.
static func initialize(addon_root: String = "") -> void:
	if not addon_root.is_empty():
		_addon_root = addon_root.replace("res://", "").trim_suffix("/")
	
	_runtime_initialized = true
	current_level = Level.INFO
	module_levels.clear()
	_load_active_profile()
	_recompute_min_level()


static func _ensure_initialized() -> void:
	if _runtime_initialized:
		return
	
	if _addon_root.is_empty():
		var stack := get_stack()
		for frame: Dictionary in stack:
			var src: String = frame.get("source", "")
			if src.ends_with("netw_log.gd"):
				_addon_root = src.get_base_dir().get_base_dir() \
					.replace("res://", "").trim_suffix("/")
				break
		
		# Robust fallback if stack trace is unavailable (e.g. release builds)
		if _addon_root.is_empty():
			_addon_root = "addons/networked"

	_runtime_initialized = true
	
	var test_override := _detect_test_log_override()
	if not test_override.is_empty():
		# Load profile first so overrides in the string can cascade over it
		_load_active_profile()
		push_setting_str(test_override)
	elif Netw.is_test_env():
		current_level = Level.NONE
		module_levels.clear()
		_recompute_min_level()
	else:
		# In regular runtime, default to INFO and load assigned profile
		current_level = Level.INFO
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
			"NetwLog: Active profile not found: '%s'\n" % path + \
			"  → The resource may have been deleted or moved.\n" + \
			"  → Clear or reassign it at Project Settings > %s" \
			% SETTING_ACTIVE_PROFILE
		)
		return

	var res = ResourceLoader.load(path)
	if res is NetwLogSettings:
		current_level = res.global_level
		module_levels = res.module_overrides.duplicate()
	else:
		push_warning(
			"NetwLog: '%s' is not a NetwLogSettings resource.\n" % path + \
			"  → Reassign it at Project Settings > %s" \
			% SETTING_ACTIVE_PROFILE
		)


## Fixes a double-prefix written by an earlier version of the editor panel.
## ([code]uid://uid://...[/code]).
static func _fix_profile_path(path: String) -> String:
	if path.begins_with("uid://uid://"):
		return path.substr("uid://".length())
	return path


static func _recompute_min_level() -> void:
	_min_active_level = current_level
	for l: int in module_levels.values():
		if l != Level.INHERIT and l < _min_active_level:
			_min_active_level = l
	
	_effective_min_level = _stack_min_level()
	_effective_global_level = _compute_effective_global_level()


static func _compute_effective_global_level() -> int:
	for i in range(_settings_stack.size() - 1, -1, -1):
		if _settings_stack[i].global_level != Level.INHERIT:
			return _settings_stack[i].global_level
	return current_level


static func _stack_min_level() -> int:
	var m: int = Level.NONE
	var found_global := false
	
	for i in range(_settings_stack.size() - 1, -1, -1):
		var top := _settings_stack[i]
		for l: int in top.module_overrides.values():
			if l != Level.INHERIT and l < m:
				m = l
		if not found_global and top.global_level != Level.INHERIT:
			if top.global_level < m:
				m = top.global_level
			found_global = true
			break
			
	if not found_global:
		for l: int in module_levels.values():
			if l != Level.INHERIT and l < m:
				m = l
		if current_level < m:
			m = current_level
			
	return m


static func is_level_active_for_module(level: int, module_path: String) -> bool:
	_ensure_initialized()
	if level < _effective_min_level:
		return false
	return level >= get_effective_level(module_path)


## Fast check to see if a level is active for a specific script path.
## Allows components to early-out before doing expensive string formatting.
static func is_level_active(level: int, script_path: String) -> bool:
	_ensure_initialized()
	if level < _effective_min_level:
		return false
	
	var module := _module_from_path(script_path)
	return level >= get_effective_level(module)


## Pushes a [NetwLogSettings] resource onto the isolation stack.
##
## Settings pushed onto the stack cascade: queries check the top of the stack
## first, and if no explicit override is found, fall back down the stack until
## reaching the base profile settings.
static func push_settings(settings: NetwLogSettings) -> void:
	_settings_stack.push_back(settings)
	_recompute_min_level()


## Pops the topmost [NetwLogSettings] from the isolation stack.
static func pop_settings() -> void:
	if not _settings_stack.is_empty():
		_settings_stack.pop_back()
	
	_recompute_min_level()


## Returns the effective log level for [param module_path].
##
## Walks up the dot-separated hierarchy until a matching override is found.
## Cascades through pushed settings before falling back to the base profile.
static func get_effective_level(module_path: String) -> int:
	_ensure_initialized()
	if _settings_stack.is_empty() and module_levels.is_empty():
		return current_level
		
	var parts := module_path.split(".")
	
	for i in range(_settings_stack.size() - 1, -1, -1):
		var top: NetwLogSettings = _settings_stack[i]
		
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
## Example: [code]NetwLog.push_setting_str("info,core.network=trace")[/code]
static func push_setting_str(logl_str: String) -> void:
	push_settings(parse_logl(logl_str))


## Parses a LOGL string into a [NetwLogSettings] resource.
static func parse_logl(logl_str: String) -> NetwLogSettings:
	var res := NetwLogSettings.new()
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


## Serializes a [NetwLogSettings] resource into a LOGL string.
static func to_logl(settings: NetwLogSettings) -> String:
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
	_ensure_initialized()
	print_rich("[color=cyan][b]--- NetwLog Configuration Dump ---[/b][/color]")
	var root_str := _addon_root if not _addon_root.is_empty() else "(empty)"
	print_rich("[color=gray]Addon Root:[/color] %s" % root_str)
	
	var base_settings := NetwLogSettings.new()
	base_settings.global_level = current_level
	base_settings.module_overrides = module_levels.duplicate()
	var base_logl := to_logl(base_settings)
	print_rich(
		"[color=gray]Base LOGL:[/color] [color=yellow]%s[/color] " % base_logl + \
		"[color=gray](copied to clipboard)[/color]"
	)
	DisplayServer.clipboard_set(base_logl)
	print_rich(
		"[color=gray]Global Level:[/color] %s" % _level_to_string(current_level)
	)
	
	if module_levels.is_empty():
		print_rich("[color=gray]Module Overrides: (none)[/color]")
	else:
		print_rich("[color=gray]Module Overrides:[/color]")
		for mod in module_levels:
			var lvl_str := _level_to_string(module_levels[mod])
			print_rich("  [color=yellow]%s[/color] = %s" % [mod, lvl_str])
			
	if not _settings_stack.is_empty():
		var stack_size := _settings_stack.size()
		print_rich("[color=gray]Settings Stack (%d layers):[/color]" % stack_size)
		for i in range(_settings_stack.size() - 1, -1, -1):
			var settings: NetwLogSettings = _settings_stack[i]
			print_rich("  [Layer %d] %s" % [i, to_logl(settings)])
			
	var min_lvl_str := _level_to_string(_effective_min_level)
	print_rich("[color=gray]Effective Min Level:[/color] %s" % min_lvl_str)
	print_rich("[color=cyan][b]---------------------------------[/b][/color]")


## Logs a [code]TRACE[/code]-level message.
## Accepts optional [param args] for [code]%[/code]-style formatting.
static func trace(msg: Variant, args: Array = []) -> void:
	_ensure_initialized()
	if Level.TRACE < _effective_min_level:
		return
	if not _is_debug:
		if Level.TRACE >= _effective_global_level:
			_print("[TRACE]", msg, args, Level.TRACE, "", "")
		return
	var ctx := _get_context()
	if Level.TRACE >= get_effective_level(ctx.module):
		_print("[TRACE]", msg, args, Level.TRACE, ctx.module, ctx.site)


## Logs a [code]DEBUG[/code]-level message.
## Accepts optional [param args] for [code]%[/code]-style formatting.
static func debug(msg: Variant, args: Array = []) -> void:
	_ensure_initialized()
	if Level.DEBUG < _effective_min_level:
		return
	if not _is_debug:
		if Level.DEBUG >= _effective_global_level:
			_print("[DEBUG]", msg, args, Level.DEBUG, "", "")
		return
	var ctx := _get_context()
	if Level.DEBUG >= get_effective_level(ctx.module):
		_print("[DEBUG]", msg, args, Level.DEBUG, ctx.module, ctx.site)


## Logs an [code]INFO[/code]-level message.
## Accepts optional [param args] for [code]%[/code]-style formatting.
static func info(msg: Variant, args: Array = []) -> void:
	_ensure_initialized()
	if Level.INFO < _effective_min_level:
		return
	if not _is_debug:
		if Level.INFO >= _effective_global_level:
			_print("[INFO]", msg, args, Level.INFO, "", "")
		return
	var ctx := _get_context()
	if Level.INFO >= get_effective_level(ctx.module):
		_print("[INFO]", msg, args, Level.INFO, ctx.module, ctx.site)


## Logs a [code]WARN[/code]-level message and calls [code]push_warning[/code].
## Accepts optional [param args] for [code]%[/code]-style formatting.
## [br][br]
## Pass a [param link_call] to preserve the editor jump-click: the callable must
## call [code]push_warning[/code] itself so the engine records the caller's
## file/line.
## [codeblock]
## NetwLog.warn("Player '%s' has no health.", [name], func(m): push_warning(m))
## [/codeblock]
static func warn(
	msg: Variant, args: Array = [], link_call: Callable = Callable()
) -> void:
	_ensure_initialized()
	if Level.WARN < _effective_min_level:
		return
	if not _is_debug:
		if Level.WARN >= _effective_global_level:
			if typeof(msg) == TYPE_CALLABLE: (msg as Callable).call()
			else: _print("[WARN]", msg, args, Level.WARN, "", "", link_call)
		return
	var ctx := _get_context()
	if Level.WARN >= get_effective_level(ctx.module):
		if typeof(msg) == TYPE_CALLABLE:
			(msg as Callable).call()
		else:
			_print("[WARN]", msg, args, Level.WARN, ctx.module, ctx.site, link_call)


## Logs an [code]ERROR[/code]-level message and calls [code]push_error[/code].
## Accepts optional [param args] for [code]%[/code]-style formatting.
## [br][br]
## Pass a [param link_call] to preserve the editor jump-click: the callable must
## call [code]push_error[/code] itself so the engine records the caller's
## file/line.
## [codeblock]
## NetwLog.error("Critical: lobby '%s' not found.", [lobby_name], func(m): push_error(m))
## [/codeblock]
static func error(
	msg: Variant, args: Array = [], link_call: Callable = Callable()
) -> void:
	_ensure_initialized()
	if Level.ERROR < _effective_min_level:
		return
	if not _is_debug:
		if Level.ERROR >= _effective_global_level:
			if typeof(msg) == TYPE_CALLABLE: (msg as Callable).call()
			else: _print("[ERROR]", msg, args, Level.ERROR, "", "", link_call)
		return
	var ctx := _get_context()
	if Level.ERROR >= get_effective_level(ctx.module):
		if typeof(msg) == TYPE_CALLABLE:
			(msg as Callable).call()
		else:
			_print(
				"[ERROR]", msg, args, Level.ERROR, ctx.module, ctx.site, link_call
			)


static func _get_context() -> Dictionary:
	var stack := get_stack()

	for i in range(1, stack.size()):
		var frame: Dictionary = stack[i]
		var source: String = frame.source
		if (source.ends_with("netw_log.gd") 
				or source.ends_with("net_component.gd") 
				or source.ends_with("tp_layer_api.gd")
				or source.ends_with("netw_dbg.gd")
				or source.ends_with("netw_handle.gd")):
			continue
		return {
			"module": _module_from_path(source),
			"site": "[%s:%d] " % [source.get_file(), frame.line]
		}
	return {"module": "", "site": ""}


## Converts a script path to a dot-separated module identifier.
## Scripts inside the addon are stored relative to the addon root so overrides
## survive the addon directory being moved.
static func _module_from_path(path: String) -> String:
	var p := path.replace("res://", "").trim_suffix("/")
	if p.ends_with(".gd"):
		p = p.left(p.length() - 3)
	if not _addon_root.is_empty() and p.begins_with(_addon_root + "/"):
		p = p.substr(_addon_root.length() + 1)
	return p.replace("/", ".")


static func _detect_test_log_override() -> String:
	if OS.has_environment("NETW_TEST_LOG"):
		return OS.get_environment("NETW_TEST_LOG")
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--netw-log="):
			return arg.split("=")[1]
	return ""


static func _print(
	prefix: String, 
	msg: Variant, 
	args: Array, 
	level: int, 
	module: String, 
	site: String, 
	link_call: Callable = Callable()
) -> void:
	var body: String = str(msg) % args if not args.is_empty() else str(msg)
	var header: String
	if not module.is_empty():
		var parts := module.split(".")
		var display := (
			".".join(parts.slice(0, parts.size() - 1)) 
			if parts.size() > 1 else module
		)
		header = "%s {%s} %s" % [prefix, display, site]
	else:
		header = "%s %s" % [prefix, site]

	match level:
		Level.ERROR:
			if not link_call.is_null():
				link_call.call(body)
			else:
				push_error(header + body)
			print_rich("[color=red][b]%s[/b][/color]%s" % [header, body])
		Level.WARN:
			if not link_call.is_null():
				link_call.call(body)
			else:
				push_warning(header + body)
			print_rich("[color=yellow]%s[/color]%s" % [header, body])
		Level.TRACE:
			print_rich("[color=#555555]%s%s[/color]" % [header, body])
		Level.DEBUG:
			print_rich("[color=gray]%s[/color]%s" % [header, body])
		_:
			print_rich("[color=white]%s[/color]%s" % [header, body])
