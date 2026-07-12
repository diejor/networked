class_name NetwScriptCache
extends RefCounted

# Per-script reflection cache. Godot's reflection calls (get_script_method_list,
# get_property_list, get_script_signal_list) are O(N) walks, so every fact they
# yield is parsed once here and reused. Lists that assign 1-byte wire ids are
# sorted so both peers agree on the mapping for identical scripts.

# The script's @rpc annotation config, cached by reference.
var rpc_config: Dictionary

var _arg_types: Dictionary = { } # method -> Array[int]
var _arity: Dictionary = { } # method -> [min, max]
var _methods_walked := false

var _prop_types: Dictionary = { } # property -> int

var _sig_arg_types: Dictionary = { } # signal -> Array[int]

var _sorted_methods: Array = []
var _sorted_methods_done := false
var _sorted_props: Array = []
var _sorted_props_done := false
var _sorted_signals: Array = []
var _sorted_signals_done := false

var _script: Script


func _init(script: Script) -> void:
	_script = script
	var cfg := script.get_rpc_config()
	rpc_config = cfg if cfg else { }


# Walks the script method list (with base scripts) once, filling arg types and
# arity for every declared method. Most-derived declaration wins.
func _walk_methods() -> void:
	if _methods_walked:
		return
	_methods_walked = true
	var s := _script
	while s != null:
		for m in s.get_script_method_list():
			var name: StringName = m["name"]
			if _arg_types.has(name):
				continue
			var types: Array = []
			for a in m["args"]:
				types.append(int(a.get("type", TYPE_NIL)))
			_arg_types[name] = types
			var total: int = m["args"].size()
			var defaults := 0
			if m.has("default_args"):
				defaults = m["default_args"].size()
			_arity[name] = [total - defaults, total]
		s = s.get_base_script()


func arg_types(method: StringName) -> Array:
	_walk_methods()
	return _arg_types.get(method, [])


# Returns [min, max] parameter counts, or [] when the method is unknown.
func arity(method: StringName) -> Array:
	_walk_methods()
	return _arity.get(method, [])


func prop_type(node: Node, property: StringName) -> int:
	if _prop_types.has(property):
		return _prop_types[property]
	var t := TYPE_NIL
	for prop in node.get_property_list():
		if prop["name"] == property:
			t = prop["type"]
			break
	_prop_types[property] = t
	return t


func signal_arg_types(signal_name: StringName) -> Array[int]:
	if _sig_arg_types.has(signal_name):
		return _sig_arg_types[signal_name]
	var types: Array[int] = []
	var s := _script
	while s != null:
		var found := false
		for sig in s.get_script_signal_list():
			if sig["name"] == signal_name:
				for arg in sig["args"]:
					types.append(int(arg.get("type", TYPE_NIL)))
				found = true
				break
		if found:
			break
		s = s.get_base_script()
	_sig_arg_types[signal_name] = types
	return types


# Sorted @rpc method names, the source of 1-byte method ids.
func sorted_methods() -> Array:
	if _sorted_methods_done:
		return _sorted_methods
	_sorted_methods_done = true
	for m in rpc_config:
		_sorted_methods.append(StringName(m))
	_sorted_methods.sort()
	return _sorted_methods


# Sorted script-declared property names, the source of 1-byte property ids.
# Covers every script variable so even zero-setup syncs compress.
func sorted_properties() -> Array:
	if _sorted_props_done:
		return _sorted_props
	_sorted_props_done = true
	var s := _script
	while s != null:
		for p in s.get_script_property_list():
			if int(p.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE:
				var n: StringName = p["name"]
				if not _sorted_props.has(n):
					_sorted_props.append(n)
		s = s.get_base_script()
	_sorted_props.sort()
	return _sorted_props


# Sorted script-declared signal names, the source of 1-byte signal ids.
func sorted_signals() -> Array:
	if _sorted_signals_done:
		return _sorted_signals
	_sorted_signals_done = true
	var s := _script
	while s != null:
		for sig in s.get_script_signal_list():
			var n: StringName = sig["name"]
			if not _sorted_signals.has(n):
				_sorted_signals.append(n)
		s = s.get_base_script()
	_sorted_signals.sort()
	return _sorted_signals
