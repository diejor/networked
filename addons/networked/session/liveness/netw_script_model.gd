## Engine-free leaf utility that caches and queries reflection data for scripts.
##
## By decoupling script reflection from active peer context, it breaks cyclic
## dependencies and allows unit-testing serialization logic without live peers.
##
## [codeblock]
## var types := NetwScriptModel.get_method_arg_types(script, &"my_method")
## var method_id := NetwScriptModel.get_method_id(script, &"my_method")
## [/codeblock]
class_name NetwScriptModel
extends RefCounted

## Defines the authority policy rules for variable synchronization and
## signal emissions.
enum Policy {
	## Only the node's multiplayer authority (the server) has permission.
	AUTHORITY = 0,
	## Only the entity's controller client has permission.
	CONTROLLER = 1,
	## Any peer has permission.
	ANY_PEER = 2,
}

## Defines the transfer reliability modes.
enum TransferMode {
	## Transmit reliably.
	RELIABLE = 0,
	## Transmit unreliably.
	UNRELIABLE = 1,
}

# Configuration registries
static var _rpc_configs: Dictionary = { }
static var _property_configs: Dictionary = { }
static var _signal_configs: Dictionary = { }
static var _spawn_fn_configs: Dictionary = { } # Script -> { method: SyncConfig }
static var _despawn_configs: Dictionary = { } # Script -> DespawnConfig
static var _persistence_configs: Dictionary = { } # Script -> PersistenceConfig
# Bare marks from Netw.mark_multiplayer_scene (Script -> true). Introspection
# only, never an authorization gate.
static var _multiplayer_scene_marks: Dictionary = { }
# On-ramp configs from Netw.configure_multiplayer_scene (Script -> SceneMarkConfig).
static var _multiplayer_scene_configs: Dictionary = { }

# Node-instance overlay: property configs for nodes that carry no script (a
# bare Node2D tracked by a synchronizer) or that need a per-instance override.
# Keyed by instance id, validated by weakref on access, consulted before the
# script-keyed statics.
static var _node_property_overlay: Dictionary = { } # int -> _NodeOverlay

# Reflection cache
static var _script_cache: Dictionary = { }


# Returns the [_ScriptCache] for [param script], building it on first use, or
# [code]null[/code] for a scriptless object.
static func _cache_for(script: Script) -> _ScriptCache:
	if not script:
		return null
	var c: _ScriptCache = _script_cache.get(script)
	if c == null:
		c = _ScriptCache.new(script)
		_script_cache[script] = c
	return c


## Resolves the RPC options (deferral, policy) for [param method] on
## [param script].
static func get_rpc_options(
		script: Script,
		method: StringName,
) -> SyncConfig:
	var methods_map = _rpc_configs.get(script)
	if methods_map and methods_map.has(method):
		return methods_map[method]
	return null


## Resolves the property options (transfer mode, policy, quantizer) for
## [param property] on [param script].
static func get_property_config(
		script: Script,
		property: StringName,
) -> SyncConfig:
	var props_map = _property_configs.get(script)
	if props_map and props_map.has(property):
		return props_map[property]
	return null


## Resolves the signal options for [param signal_name] on [param script].
static func get_signal_config(
		script: Script,
		signal_name: StringName,
) -> SyncConfig:
	var sigs_map = _signal_configs.get(script)
	if sigs_map and sigs_map.has(signal_name):
		return sigs_map[signal_name]
	return null


## Returns every RPC config declared for [param script].
static func get_rpc_configs(script: Script) -> Dictionary:
	return _rpc_configs.get(script, { }) if script else { }


## Returns the [NetwScriptModel.SceneMarkConfig] declared for [param script]
## through [method Netw.configure_multiplayer_scene], or [code]null[/code].
static func get_scene_config(script: Script) -> SceneMarkConfig:
	return _multiplayer_scene_configs.get(script, null) if script else null


## Returns whether [param script] is registered as a multiplayer scene root by
## either [method Netw.mark_multiplayer_scene] or
## [method Netw.configure_multiplayer_scene].
static func is_scene_marked(script: Script) -> bool:
	if script == null:
		return false
	return _multiplayer_scene_marks.has(script) \
			or _multiplayer_scene_configs.has(script)


## Returns every property config declared for [param script].
static func get_property_configs(script: Script) -> Dictionary:
	return _property_configs.get(script, { }) if script else { }


## Returns every signal config declared for [param script].
static func get_signal_configs(script: Script) -> Dictionary:
	return _signal_configs.get(script, { }) if script else { }


## Resolves the spawn-function config registered through
## [method Netw.configure_spawn] for [param method] on [param script].
static func get_spawn_config(
		script: Script,
		method: StringName,
) -> SyncConfig:
	var methods_map = _spawn_fn_configs.get(script)
	if methods_map and methods_map.has(method):
		return methods_map[method]
	return null


## Resolves the [NetwScriptModel.DespawnConfig] registered through
## [method Netw.configure_despawn] for [param script], walking base scripts.
static func get_despawn_config(script: Script) -> DespawnConfig:
	var s := script
	while s != null:
		if _despawn_configs.has(s):
			return _despawn_configs[s]
		s = s.get_base_script()
	return null


## Resolves the [NetwScriptModel.PersistenceConfig] registered through
## [method Netw.configure_persistence] for [param node], consulting the
## per-node overlay before the script-keyed statics and walking base scripts.
static func get_persistence_config(node: Node) -> PersistenceConfig:
	var overlay := _overlay_for(node, false)
	if overlay and overlay.persistence:
		return overlay.persistence
	var s := node.get_script() as Script
	while s != null:
		if _persistence_configs.has(s):
			return _persistence_configs[s]
		s = s.get_base_script()
	return null


## Resolves or creates the per-node [NetwScriptModel.PersistenceConfig] for [param node], stored
## in the node-instance overlay. The scriptless path for
## [method Netw.configure_persistence].
static func configure_node_persistence(node: Node) -> PersistenceConfig:
	var overlay := _overlay_for(node, true)
	if overlay.persistence:
		return overlay.persistence
	overlay.persistence = PersistenceConfig.new()
	return overlay.persistence


## Resolves or creates the per-instance property [NetwScriptModel.PropertyConfig] for
## [param property] on [param node], stored in the node-instance overlay.
static func configure_node_property(
		node: Node,
		property: StringName,
) -> PropertyConfig:
	var overlay := _overlay_for(node, true)
	var existing := overlay.configs.get(property) as PropertyConfig
	if existing:
		return existing
	var opt := PropertyConfig.new()
	opt.is_property = true
	opt.context_script = node.get_script() as Script
	opt.context_name = property
	opt.context_type = 1
	opt.context_node_ref = weakref(node)
	overlay.configs[property] = opt
	return opt


## Returns the property configs for [param node], the overlay merged over any
## script-declared configs. Overlay entries win on key collision.
static func get_node_property_configs(node: Node) -> Dictionary:
	var merged: Dictionary = { }
	var script := node.get_script() as Script
	if script:
		for property: StringName in get_property_configs(script):
			merged[property] = get_property_configs(script)[property]
	var overlay := _overlay_for(node, false)
	if overlay:
		for property: StringName in overlay.configs:
			merged[property] = overlay.configs[property]
	return merged


## Returns the first [NetwInterpolate] for [param property] on [param node],
## consulting the node-instance overlay before the script-keyed configs.
static func get_node_property_interpolator(
		node: Node,
		property: StringName,
) -> NetwInterpolate:
	var overlay := _overlay_for(node, false)
	if overlay:
		var opt := overlay.configs.get(property) as SyncConfig
		if opt and not opt.interpolators.is_empty():
			return opt.interpolators[0]
	return get_property_interpolator(node.get_script() as Script, property)


## Drops the overlay entry for [param node] on despawn.
static func clear_node_overlay(node: Node) -> void:
	if is_instance_valid(node):
		_node_property_overlay.erase(node.get_instance_id())


## Removes overlay entries whose node has been freed.
static func sweep_dead_overlays() -> void:
	for id: int in _node_property_overlay.keys():
		var overlay := _node_property_overlay[id] as _NodeOverlay
		if not overlay or overlay.node_ref.get_ref() == null:
			_node_property_overlay.erase(id)


# Resolves the overlay for [param node], validating the weakref so a reused
# instance id never returns a stale entry. Creates one when [param create].
static func _overlay_for(node: Node, create: bool) -> _NodeOverlay:
	var id := node.get_instance_id()
	var overlay := _node_property_overlay.get(id) as _NodeOverlay
	if overlay and overlay.node_ref.get_ref() == node:
		return overlay
	if not create:
		return null
	overlay = _NodeOverlay.new()
	overlay.node_ref = weakref(node)
	_node_property_overlay[id] = overlay
	return overlay


## Returns the [NetwInterpolate] list for [param method].
static func get_method_interpolators(
		script: Script,
		method: StringName,
) -> Array:
	var opt := get_rpc_options(script, method)
	return opt.interpolators if opt else []


## Returns the first [NetwInterpolate] for [param property].
static func get_property_interpolator(
		script: Script,
		property: StringName,
) -> NetwInterpolate:
	var opt := get_property_config(script, property)
	if opt and not opt.interpolators.is_empty():
		return opt.interpolators[0]
	return null


## Returns the [NetwInterpolate] list for [param signal_name].
static func get_signal_interpolators(
		script: Script,
		signal_name: StringName,
) -> Array:
	var opt := get_signal_config(script, signal_name)
	return opt.interpolators if opt else []


## Extracts the native rpc_mode for [param method] on [param script] from the
## @rpc annotation config.
static func get_method_rpc_mode(script: Script, method: StringName) -> int:
	var c := _cache_for(script)
	if c and c.rpc_config.has(method):
		var info = c.rpc_config[method]
		if info is Dictionary and info.has("rpc_mode"):
			return info["rpc_mode"]
	return 0


## Returns [code]true[/code] when [param method] on [param script] is annotated
## with a reliable transfer mode. Godot's [code]transfer_mode[/code] is
## [code]2[/code] for reliable and [code]0[/code]/[code]1[/code] for the
## unreliable variants. Methods with no [code]@rpc[/code] config default to
## reliable, the safe choice for discrete calls.
static func get_method_reliable(script: Script, method: StringName) -> bool:
	var c := _cache_for(script)
	if c and c.rpc_config.has(method):
		var info = c.rpc_config[method]
		if info is Dictionary and info.has("transfer_mode"):
			return int(info["transfer_mode"]) == 2
	return true


## Returns [code]true[/code] when [param method] on [param script] is configured
## to be called locally as well.
static func get_method_call_local(script: Script, method: StringName) -> bool:
	var c := _cache_for(script)
	if c and c.rpc_config.has(method):
		var info = c.rpc_config[method]
		if info is Dictionary and info.has("call_local"):
			return bool(info["call_local"])
	return false


## Validates if the argument count matches the script signature.
static func validate_argument_count(
		script: Script,
		method: StringName,
		arg_count: int,
) -> bool:
	var c := _cache_for(script)
	if not c:
		return false
	var arity := c.arity(method)
	if arity.is_empty():
		return false
	return arg_count >= arity[0] and arg_count <= arity[1]


## Helper to assign and retrieve 1-byte method IDs per script.
static func get_method_id(script: Script, method: StringName) -> int:
	var list := _get_sorted_methods(script)
	var idx := list.find(method)
	if idx >= 0 and idx < 255:
		return idx + 1
	return 0


## Helper to retrieve the method name from its 1-byte ID.
static func get_method_name_by_id(
		script: Script,
		method_id: int,
) -> StringName:
	var list := _get_sorted_methods(script)
	var idx := method_id - 1
	if idx >= 0 and idx < list.size():
		return list[idx]
	return &""


## Returns the declared parameter [enum Variant.Type] list for [param method]
## on [param script], used to reconstruct quantized arguments on the receiver.
## An untyped parameter reports [constant TYPE_NIL].
static func get_method_arg_types(script: Script, method: StringName) -> Array:
	var c := _cache_for(script)
	return c.arg_types(method) if c else []


## Returns the native [enum Variant.Type] for [param property] on [param node].
static func get_node_property_type(node: Node, property: StringName) -> int:
	var c := _cache_for(node.get_script() as Script)
	if c:
		return c.prop_type(node, property)
	for prop in node.get_property_list():
		if prop["name"] == property:
			return prop["type"]
	return TYPE_NIL


## Returns the declared parameter [enum Variant.Type] list for
## [param signal_name] on [param script].
static func get_signal_arg_types(
		script: Script,
		signal_name: StringName,
) -> Array[int]:
	var c := _cache_for(script)
	return c.signal_arg_types(signal_name) if c else [] as Array[int]


## Helper to assign and retrieve 1-byte property IDs per script.
static func get_property_id(script: Script, property: StringName) -> int:
	var c := _cache_for(script)
	if not c:
		return 0
	var idx := c.sorted_properties().find(property)
	if idx >= 0 and idx < 255:
		return idx + 1
	return 0


## Helper to retrieve the property name from its 1-byte ID.
static func get_property_name_by_id(
		script: Script,
		property_id: int,
) -> StringName:
	var c := _cache_for(script)
	if not c:
		return &""
	var list := c.sorted_properties()
	var idx := property_id - 1
	if idx >= 0 and idx < list.size():
		return list[idx]
	return &""


## Helper to assign and retrieve 1-byte signal IDs per script.
static func get_signal_id(script: Script, signal_name: StringName) -> int:
	var c := _cache_for(script)
	if not c:
		return 0
	var idx := c.sorted_signals().find(signal_name)
	if idx >= 0 and idx < 255:
		return idx + 1
	return 0


## Helper to retrieve the signal name from its 1-byte ID.
static func get_signal_name_by_id(
		script: Script,
		signal_id: int,
) -> StringName:
	var c := _cache_for(script)
	if not c:
		return &""
	var list := c.sorted_signals()
	var idx := signal_id - 1
	if idx >= 0 and idx < list.size():
		return list[idx]
	return &""


static func _get_sorted_methods(script: Script) -> Array:
	var c := _cache_for(script)
	return c.sorted_methods() if c else []


## Writes a list of values: each is prefixed by a flag byte, 1 when the value
## is bit-packed by its quantizer and 0 when it rides NetwCodec's tagged
## fallback.
static func write_values(
		w: NetwBitBuffer.Writer,
		values: Array,
		quantizers: Array,
		types: Array,
) -> void:
	w.put_aligned_u8(values.size())
	for i in values.size():
		# A node reference is a distinct wire kind, so a node argument crosses as
		# its route and component address instead of a serialized value.
		if values[i] is NetwNodeRef:
			w.put_aligned_u8(2)
			_write_node_ref(w, values[i])
			continue
		var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
		var t: int = types[i] if i < types.size() else TYPE_NIL
		var quantized := q != null and q._supports_type(t) \
				and q._supports_type(typeof(values[i]) as Variant.Type)
		if quantized:
			w.put_aligned_u8(1)
			NetwCodec.encode_value(w, values[i], q)
		else:
			w.put_aligned_u8(0)
			NetwCodec.encode_value(w, values[i], null)


## Reads a list of values written by [method write_values].
static func read_values(
		r: NetwBitBuffer.Reader,
		quantizers: Array,
		types: Array,
) -> Array:
	var count := r.get_aligned_u8()
	var out: Array = []
	for i in count:
		var flag := r.get_aligned_u8()
		if flag == 2:
			out.append(_read_node_ref(r))
		elif flag == 1:
			var q: NetwQuantize = quantizers[i] if i < quantizers.size() else null
			var t: int = types[i] if i < types.size() else TYPE_NIL
			out.append(NetwCodec.decode_value(r, t, q))
		else:
			out.append(NetwCodec.decode_value(r, TYPE_NIL, null))
	return out


# Writes a node reference: route varint, component byte, and a path string only
# when the component byte is the 255 path fallback.
static func _write_node_ref(w: NetwBitBuffer.Writer, ref: NetwNodeRef) -> void:
	NetwCodec.put_varint(w, ref.route)
	w.put_aligned_u8(ref.comp)
	if ref.comp == 255:
		var pb := ref.path.to_utf8_buffer()
		w.put_aligned_u32(pb.size())
		w.put_aligned_bytes(pb)


# Reads a node reference written by _write_node_ref.
static func _read_node_ref(r: NetwBitBuffer.Reader) -> NetwNodeRef:
	var route := NetwCodec.get_safe_varint(r)
	var comp := r.get_aligned_u8()
	var path := ""
	if comp == 255:
		var plen := r.get_aligned_u32()
		path = r.get_aligned_bytes(plen).get_string_from_utf8()
	return NetwNodeRef.new(route, comp, path)


## Writes a token (1-byte ID or StringName).
static func write_token(w: NetwBitBuffer.Writer, token: Variant) -> void:
	if token is int:
		w.put_aligned_u8(1)
		w.put_aligned_u8(int(token))
	else:
		w.put_aligned_u8(0)
		var nb := String(token).to_utf8_buffer()
		w.put_aligned_u32(nb.size())
		w.put_aligned_bytes(nb)


## Reads a token written by [method write_token].
static func read_token(r: NetwBitBuffer.Reader) -> Variant:
	var flag := r.get_aligned_u8()
	if flag == 1:
		return r.get_aligned_u8()
	var name_len := r.get_aligned_u32()
	return StringName(r.get_aligned_bytes(name_len).get_string_from_utf8())


## Writes a CALL body: method token, then a length-tagged, per-argument stream.
static func write_call_body(
		w: NetwBitBuffer.Writer,
		method_val: Variant,
		encoded_args: Array,
		quantizers: Array,
		arg_types: Array,
) -> void:
	write_token(w, method_val)
	write_values(w, encoded_args, quantizers, arg_types)


## Reads the method token written by [method write_call_body], returning the
## 1-byte id as an int or the method name as a StringName.
static func read_method_token(r: NetwBitBuffer.Reader) -> Variant:
	var flag := r.get_aligned_u8()
	if flag == 1:
		return r.get_aligned_u8()
	var name_len := r.get_aligned_u32()
	return StringName(r.get_aligned_bytes(name_len).get_string_from_utf8())


## Reads the per-argument stream written by [method write_call_body]. Quantized
## arguments reconstruct through their declared parameter type. The stream is
## identical to [method write_values] output, so this delegates to
## [method read_values].
static func read_call_args(
		r: NetwBitBuffer.Reader,
		quantizers: Array,
		arg_types: Array,
) -> Array:
	return read_values(r, quantizers, arg_types)


## Validates a per-position [param quantizers] list against a parallel
## [param types] list, warning and returning [code]false[/code] on the first
## quantizer that cannot pack its declared type. A null quantizer slot or a
## [constant TYPE_NIL] type is always accepted. Shared by
## [NetwScriptModel.SyncConfig] and [NetwScriptModel.ConnectConfig] so both
## surfaces enforce one rule. [param context_name] and [param context_script]
## only name the site in the warning.
static func validate_quantizers(
		quantizers: Array,
		types: Array,
		context_name: StringName,
		context_script: Script,
) -> bool:
	for i in quantizers.size():
		var q: NetwQuantize = quantizers[i]
		if q == null:
			continue
		var t: int = types[i] if i < types.size() else TYPE_NIL
		if t != TYPE_NIL and not q._supports_type(t):
			Netw.dbg.warn(
				"SyncConfig.quantize: Quantizer of type '%s' "
				+ "does not support the declared type '%s' for "
				+ "'%s' on script '%s'.",
				[
					q.get_class(),
					type_string(t),
					context_name,
					context_script.resource_path.get_file() if context_script else "",
				],
				func(m): push_warning(m)
			)
			return false
	return true


## Delivery axes for one replicated signal, RPC, or property.
##
## [method Netw.configure_rpc] and [method Netw.configure_signal] return this
## builder directly, while [method Netw.configure_property] returns the
## [PropertyConfig] subtype that adds the property-only axes on top. One fluent
## chain in [method Object._init] declares who may author the stream
## ([method authority], [method controller], [method any_peer]), how it travels
## ([method reliable], [method unreliable], [method quantize]), and what the
## receiver does with it ([method defer_until], [method interpolate]).
## [codeblock]
## func _init() -> void:
##     Netw.configure_rpc(self.fire).unreliable().controller_only()
##     Netw.configure_signal(self.exploded).any_peer()
##     Netw.configure_property(self, &"pet_name").on_spawn()
## [/codeblock]
## Configuration is keyed per script while [method Object._init] runs per
## instance, so every spawn re-declares onto the same config. Re-declaring the
## same value is silent, and only a genuine disagreement between two call sites
## warns.
class SyncConfig:
	extends RefCounted

	## The write/emit policy rule. Re-declaring the same value is silent, so a
	## script configuring in [method Object._init] re-declares per instance
	## without noise. Only a genuine disagreement warns.
	var write_policy: Policy = Policy.AUTHORITY:
		set(val):
			if _policy_configured and write_policy != val:
				Netw.dbg.warn(
					"SyncConfig: policy overridden multiple times",
					func(m): push_warning(m)
				)
			write_policy = val
			_policy_configured = true
	var _policy_configured := false

	## The transfer reliability mode. Same-value re-declaration is silent, like
	## [member write_policy].
	var transfer_mode: TransferMode = TransferMode.RELIABLE:
		set(val):
			if _transfer_configured and transfer_mode != val:
				Netw.dbg.warn(
					"SyncConfig: transfer mode overridden multiple times",
					func(m): push_warning(m)
				)
			transfer_mode = val
			_transfer_configured = true
	var _transfer_configured := false

	## If [code]true[/code], the signal/RPC is emitted/executed locally on
	## the sender. Same-value re-declaration is silent, like
	## [member write_policy].
	var is_call_local: bool = false:
		set(val):
			if _local_configured and is_call_local != val:
				Netw.dbg.warn(
					"SyncConfig: call_local/call_remote overridden multiple times",
					func(m): push_warning(m)
				)
			is_call_local = val
			_local_configured = true
	var _local_configured := false

	# Context for warnings and validation checks
	var context_script: Script = null
	var context_name: StringName = &""
	var context_type: int = 0 # 0 = RPC, 1 = Property, 2 = Signal
	var context_node_ref: WeakRef = null

	## The StringName of the signal this RPC call is deferred until.
	var defer_signal_name: StringName = &""

	## If [code]true[/code], only the controller client or server may call
	## this RPC.
	var is_controller_only: bool = false

	## List of per-argument quantizers used to compress payloads.
	var quantizers: Array = []

	## List of per-argument interpolators used to smooth received values.
	var interpolators: Array = []


	## Only the node's multiplayer authority (the server) may write/emit.
	## The default.
	func authority() -> SyncConfig:
		write_policy = Policy.AUTHORITY
		return self


	## Only the entity's [member NetwEntity.controller] may write/emit.
	func controller() -> SyncConfig:
		write_policy = Policy.CONTROLLER
		return self


	## Any peer may write/emit.
	func any_peer() -> SyncConfig:
		write_policy = Policy.ANY_PEER
		return self


	## Transmit reliably. The default.
	func reliable() -> SyncConfig:
		transfer_mode = TransferMode.RELIABLE
		return self


	## Transmit unreliably.
	func unreliable() -> SyncConfig:
		transfer_mode = TransferMode.UNRELIABLE
		return self


	## The signal/RPC is emitted/executed locally on the sender as well.
	##
	## [br][br][b]Note:[/b] [method Netw.configure_signal] configurations
	## default to [method call_local].
	func call_local() -> SyncConfig:
		is_call_local = true
		return self


	## The signal/RPC is only replicated to remote peers and not executed
	## locally.
	##
	## [br][br][b]Note:[/b] [method Netw.configure_rpc] defaults to
	## [method call_remote].
	func call_remote() -> SyncConfig:
		is_call_local = false
		return self


	## Holds a received call until [param sig] has fired on the target node,
	## instead of running it the moment the node is live. [code]self.ready[/code]
	## is the common choice.
	func defer_until(sig: Signal) -> SyncConfig:
		if not defer_signal_name.is_empty():
			Netw.dbg.warn(
				"SyncConfig: defer gate overridden multiple times",
				func(m): push_warning(m)
			)
		defer_signal_name = sig.get_name()
		return self


	## Restricts the caller to the entity's [member NetwEntity.controller]
	## (or the server).
	func controller_only() -> SyncConfig:
		is_controller_only = true
		return self


	## Bit-packs arguments with a per-position [NetwQuantize] list, parallel
	## to the parameters.
	##
	## Configuration is keyed per script while [method Object._init] runs per
	## instance, so a script that quantizes its marks in [method Object._init]
	## re-declares onto the same config with every spawn. Re-declaring a
	## layout-equal list ([method NetwQuantize.is_same_layout]) is silent. Only
	## a re-declaration that changes the bit layout warns, since that is two
	## call sites genuinely disagreeing about one property's schema.
	func quantize(p_quantizers: Variant) -> SyncConfig:
		var new_list: Array
		if p_quantizers is Array:
			new_list = p_quantizers
		elif p_quantizers is NetwQuantize:
			new_list = [p_quantizers]
		else:
			assert(
				false,
				"quantize: Expected NetwQuantize or Array of NetwQuantize",
			)
		if not quantizers.is_empty() \
				and not _same_quantizer_layout(quantizers, new_list):
			Netw.dbg.warn(
				"SyncConfig: quantizers overridden with a different layout",
				func(m): push_warning(m)
			)
		quantizers = new_list
		assert(
			_validate_quantizer_support(),
			"SyncConfig: Incompatible quantizer configured.",
		)
		return self


	# Pairwise layout equality over two quantizer lists, treating a null slot
	# (self-describing raw) as equal only to another null slot.
	static func _same_quantizer_layout(a: Array, b: Array) -> bool:
		if a.size() != b.size():
			return false
		for i in a.size():
			var qa := a[i] as NetwQuantize
			var qb := b[i] as NetwQuantize
			if qa == null or qb == null:
				if qa != qb:
					return false
				continue
			if not qa.is_same_layout(qb):
				return false
		return true


	## Smooths received values with a per-position [NetwInterpolate] list.
	##
	## Re-applying the same interpolators is idempotent, so an authoring shell
	## can safely push its declared specs again after a reparent.
	func interpolate(p_interpolators: Variant) -> SyncConfig:
		var new_list: Array
		if p_interpolators is Array:
			new_list = p_interpolators
		elif p_interpolators is NetwInterpolate:
			new_list = [p_interpolators]
		else:
			assert(
				false,
				"interpolate: Expected NetwInterpolate or Array of "
				+ "NetwInterpolate",
			)
			return self
		if _same_interpolator_specs(new_list, interpolators):
			return self
		if not interpolators.is_empty():
			Netw.dbg.warn(
				"SyncConfig: interpolators overridden multiple times",
				func(m): push_warning(m)
			)
		interpolators = new_list
		assert(
			_validate_interpolator_support(),
			"SyncConfig: Incompatible interpolator configured.",
		)
		return self


	# True when two interpolator lists declare the same specs in the same order,
	# compared by value so re-declaring freshly built but identical specs is
	# idempotent rather than a reference mismatch that warns and re-assigns.
	func _same_interpolator_specs(a: Array, b: Array) -> bool:
		if a.size() != b.size():
			return false
		for i in a.size():
			var ia := a[i] as NetwInterpolate
			var ib := b[i] as NetwInterpolate
			if ia == null or not ia.is_same_spec(ib):
				return false
		return true


	## True when this config only smooths a value and never claims a write.
	##
	## An interpolation-only spec carries interpolators but no explicit write
	## policy, transfer mode, or quantizer, so smoothing a property a
	## synchronizer already replicates is expected, not double authority.
	func is_interpolation_only() -> bool:
		return (
				not interpolators.is_empty()
				and not _policy_configured
				and not _transfer_configured
				and quantizers.is_empty()
		)


	func _validate_quantizer_support() -> bool:
		if context_name.is_empty():
			return true

		var types: Array = []
		if context_type == 0:
			types = NetwScriptModel.get_method_arg_types(
				context_script,
				context_name,
			)
		elif context_type == 1:
			var prop_type := TYPE_NIL
			var node = (
					context_node_ref.get_ref()
					if context_node_ref
					else null
			)
			if node:
				for prop in node.get_property_list():
					if prop["name"] == context_name:
						prop_type = prop["type"]
						break
			types = [prop_type]
		elif context_type == 2:
			types = NetwScriptModel.get_signal_arg_types(
				context_script,
				context_name,
			)

		return NetwScriptModel.validate_quantizers(
			quantizers,
			types,
			context_name,
			context_script,
		)


	func _validate_interpolator_support() -> bool:
		if context_name.is_empty():
			return true

		var types: Array = []
		if context_type == 0:
			types = NetwScriptModel.get_method_arg_types(
				context_script,
				context_name,
			)
		elif context_type == 1:
			var prop_type := TYPE_NIL
			var node = (
					context_node_ref.get_ref()
					if context_node_ref
					else null
			)
			if node:
				for prop in node.get_property_list():
					if prop["name"] == context_name:
						prop_type = prop["type"]
						break
			types = [prop_type]
		elif context_type == 2:
			types = NetwScriptModel.get_signal_arg_types(
				context_script,
				context_name,
			)

		for i in interpolators.size():
			var interp: NetwInterpolate = interpolators[i]
			if interp == null:
				continue
			var t: int = types[i] if i < types.size() else TYPE_NIL
			if t != TYPE_NIL and not interp._supports_type(t):
				Netw.dbg.warn(
					"SyncConfig.interpolate: Interpolator does not "
					+ "support the declared type '%s' for '%s' on "
					+ "script '%s'.",
					[
						type_string(t),
						context_name,
						context_script.resource_path.get_file(),
					],
					func(m): push_warning(m)
				)
				return false
		return true


## The property face of [NetwScriptModel.SyncConfig]: one fluent declaration in
## [method Object._init] that decides how a script variable travels for the rest
## of its life.
##
## Marking a field with [method state], [method input], or [method broadcast]
## enrolls it in one of the script's per-tick sets, and from then on the sync
## pump on [NetwSyncPipeline] ships every change automatically. Writing the
## variable is the whole job, there is no send call in the gameplay code.
## [codeblock]
## func _init() -> void:
##     Netw.configure_property(self, &"position").state().quantize(pos_q)
##     Netw.configure_property(self, &"move_dir").input()
##     Netw.configure_property(self, &"aim_dir").broadcast()
##
## func _physics_process(_dt: float) -> void:
##     move_dir = Input.get_vector("left", "right", "up", "down")
##     aim_dir = (get_global_mouse_position() - position).normalized()
##     # no send call: the next pump tick ships every marked change
## [/codeblock]
## Pick the kind by answering who owns the value, who sees it, and whether the
## server polices it. [enum NetwSyncSet.Record] holds the comparison table. A
## field with no kind mark rides no per-tick set and syncs only when
## [method Netw.sync_property] pushes it explicitly.
##
## [br][br]
## [method volatile], [method retained], [method epsilon], and
## [method persisted] refine one property. [method every_tick],
## [method on_change], [method heartbeat], [method windowed], [method audience],
## and [method masked] write through to the script's whole [NetwSyncSet] from
## any member, so the last member to name a knob owns it.
class PropertyConfig:
	extends SyncConfig

	## Sentinel for an integer knob no member has written yet.
	const UNSET := -1

	## Always [code]true[/code] on a property config. Distinguishes a
	## [PropertyConfig] from a plain [NetwScriptModel.SyncConfig] (the config an
	## RPC or signal returns) when only the base type is known.
	var is_property: bool = true

	## If [code]true[/code], the property's value rides the
	## [constant NetwFrameEnvelope.Channel.SPAWN] frame and is applied on every
	## receiving peer before the node enters the tree. Set by [method on_spawn].
	var is_spawn_state: bool = false

	## The field's delivery lane: [constant NetwSyncSet.Lane.VOLATILE] freshest
	## wins, [constant NetwSyncSet.Lane.RETAINED] reliable on change.
	var lane: NetwSyncSet.Lane = NetwSyncSet.Lane.VOLATILE

	## True once [method state] marks this property into its script's state set.
	var in_state_set: bool = false

	## True once [method input] marks this property into its script's input set.
	var in_input_set: bool = false

	## True once [method broadcast] marks this property into its script's broadcast
	## set, the trusted display stream outside the rewind boundary.
	var in_broadcast_set: bool = false

	## The field's own divergence threshold, or a negative value to inherit the
	## entity's [member PredictionComponent.divergence_epsilon]. Set by
	## [method epsilon].
	var epsilon_override: float = -1.0

	## The script set's [member NetwSyncSet.trigger], or [constant UNSET]. Written
	## by [method every_tick] and [method on_change].
	var set_trigger: int = UNSET

	## The script set's every-tick throttle in seconds, or a negative value when
	## unset. Written by [method every_tick].
	var set_every_tick_interval: float = -1.0

	## The script set's heartbeat interval in ticks, or [constant UNSET]. Written
	## by [method heartbeat].
	var set_heartbeat_ticks: int = UNSET

	## The script set's [member NetwSyncSet.window], or [constant UNSET]. Written
	## by [method windowed].
	var set_window: int = UNSET

	## The script set's [member NetwSyncSet.audience]. Written by
	## [method audience] and the [method input] preset.
	var set_audience: NetwSyncSet.Audience = NetwSyncSet.Audience.AUDIENCE_PUBLIC

	## The script set's [member NetwSyncSet.masked]. Written by [method masked].
	var set_masked: bool = false

	## True once [method persisted] marks this field a persistence column. The
	## field becomes a schema column [NetwPersistenceInterface] snapshots and
	## hydrates, independent of whether it also syncs.
	var is_persisted: bool = false

	## Per-field snapshot cadence in seconds, or [code]0.0[/code] to inherit the
	## archetype's [NetwScriptModel.PersistenceConfig] interval. Set by [method persisted].
	var persist_interval: float = 0.0

	## What the value does in the simulation, which decides whether a
	## reconciliation compares and restores it. Set by [method causal],
	## [method derived], and [method cosmetic].
	var property_class: NetwSyncSet.PropertyClass = NetwSyncSet.PropertyClass.CAUSAL

	## How firmly a recovery pulls this value toward the authoritative one rather
	## than writing it outright, or [code]0.0[/code] to write it. Set by
	## [method converge].
	var converge_stiffness: float = 0.0

	## True once [method teleport_only] restricts the field to teleport-tier
	## recoveries.
	var explicit_teleport_only: bool = false

	## True once [method reconcile_only] excludes the field from triggering a
	## correction on its own.
	var explicit_reconcile_only: bool = false


	## The server owns this value, every observer sees it, and hit detection can
	## rewind it. The authoritative kind, the body pose the whole game agrees on.
	## See [enum NetwSyncSet.Record] to compare kinds.
	## [codeblock]
	## func _init() -> void:
	##     Netw.configure_property(self, &"position").state().quantize(pos_q)
	##
	## func _physics_process(dt: float) -> void:
	##     position += velocity * dt
	##     # on the server that write is enough: the pump tick replicates it,
	##     # and every client's copy of this node follows
	## [/codeblock]
	## A state field is also the reconciliation anchor: a client predicting this
	## entity is corrected against the server's stream, within the tolerance
	## [method epsilon] grants.
	func state() -> PropertyConfig:
		in_state_set = true
		lane = NetwSyncSet.Lane.VOLATILE
		set_trigger = NetwSyncSet.Trigger.TRIGGER_ON_CHANGE
		return self


	## The controller owns this value, only the server sees it, and the server
	## re-runs it to verify. The controls kind, the command stream a client is
	## trusted to author but never to resolve. See [enum NetwSyncSet.Record] to
	## compare kinds.
	## [codeblock]
	## func _init() -> void:
	##     Netw.configure_property(self, &"move_dir").input()
	##
	## func _physics_process(dt: float) -> void:
	##     if entity.is_controlled_locally:
	##         move_dir = Input.get_vector("left", "right", "up", "down")
	##     # both peers run this: the client predicts, the server decides
	##     velocity = move_dir * SPEED
	## [/codeblock]
	## An input set is windowed by default so a lost tick heals from a redundant
	## sample instead of a retransmit. [method windowed] resizes that window.
	func input() -> PropertyConfig:
		in_input_set = true
		lane = NetwSyncSet.Lane.VOLATILE
		set_trigger = NetwSyncSet.Trigger.TRIGGER_ON_CHANGE
		set_audience = NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY
		return self


	## The controller owns this value, every observer sees it, and nobody checks
	## it. The display kind, for cosmetic streams like an aim arrow or a look
	## direction where being wrong costs nothing, so the server keeps no history
	## and never rewinds it. See [enum NetwSyncSet.Record] to compare kinds.
	## [codeblock]
	## func _init() -> void:
	##     Netw.configure_property(self, &"aim_dir").broadcast()
	##
	## func _process(_dt: float) -> void:
	##     aim_dir = (get_global_mouse_position() - position).normalized()
	##     # nothing else to do: each pump tick broadcasts the change and
	##     # every other player's copy of this node updates its arrow
	## [/codeblock]
	## Add [method masked] when many viewers watch the same entity, so each one
	## receives only the fields that changed for them.
	func broadcast() -> PropertyConfig:
		in_broadcast_set = true
		lane = NetwSyncSet.Lane.VOLATILE
		set_trigger = NetwSyncSet.Trigger.TRIGGER_ON_CHANGE
		return self


	## Routes the field onto the [constant NetwSyncSet.Lane.VOLATILE]
	## freshest-wins lane, right for a value that changes continuously, where a
	## lost sample is superseded by the next tick anyway. The default lane.
	func volatile() -> PropertyConfig:
		lane = NetwSyncSet.Lane.VOLATILE
		return self


	## Routes the field onto the [constant NetwSyncSet.Lane.RETAINED]
	## reliable-on-change lane, right for a discrete value that changes rarely and
	## must never miss a change.
	## [codeblock]
	## # a dropped "stunned = true" would desync forever, so it rides reliably
	## Netw.configure_property(self, &"stunned").state().retained()
	## [/codeblock]
	func retained() -> PropertyConfig:
		lane = NetwSyncSet.Lane.RETAINED
		return self


	## Marks the value an antecedent of the simulation, one the next step reads
	## from, so a reconciliation compares it and restores it. The default class.
	## [codeblock]
	## # the body integrates from velocity, so a restore that skipped it would
	## # rebase the position and then immediately drift away from it again
	## Netw.configure_property(self, &"velocity").state().causal()
	## [/codeblock]
	## See [enum NetwSyncSet.PropertyClass] to compare classes.
	func causal() -> PropertyConfig:
		property_class = NetwSyncSet.PropertyClass.CAUSAL
		return self


	## Marks the value one the body recomputes from causal fields each step, so a
	## reconciliation replicates it for observers but never restores it. Writing
	## it back would set a value the next step overwrites anyway.
	## [codeblock]
	## # recomputed from velocity every tick, so restoring it decides nothing
	## Netw.configure_property(self, &"speed").state().derived()
	## [/codeblock]
	## See [enum NetwSyncSet.PropertyClass] to compare classes.
	func derived() -> PropertyConfig:
		property_class = NetwSyncSet.PropertyClass.DERIVED
		return self


	## Marks the value display-only, so no reconciliation compares it and none
	## restores it. A cosmetic field that disagreed would otherwise correct a
	## simulation over a value no simulation reads.
	## [codeblock]
	## Netw.configure_property(self, &"skid_intensity").state().cosmetic()
	## [/codeblock]
	## See [enum NetwSyncSet.PropertyClass] to compare classes.
	func cosmetic() -> PropertyConfig:
		property_class = NetwSyncSet.PropertyClass.COSMETIC
		return self


	## Pulls the field toward the authoritative value at [param stiffness] during
	## a recovery instead of writing it outright, bounded to that one recovery.
	## [codeblock]
	## Netw.configure_property(self, &"heading").state().causal().converge(0.4)
	## [/codeblock]
	## A stiffness of [code]0.0[/code] restores the value outright, the default.
	func converge(stiffness: float) -> PropertyConfig:
		converge_stiffness = stiffness
		return self


	## Restricts the field to teleport-tier recoveries, so an ordinary one leaves
	## it alone. Right for a value whose mid-flight rewrite is more disruptive
	## than the drift it would correct.
	## [codeblock]
	## Netw.configure_property(self, &"angular_velocity").state().teleport_only()
	## [/codeblock]
	func teleport_only() -> PropertyConfig:
		explicit_teleport_only = true
		return self


	## Sets this field's own divergence [param threshold], the distance its
	## predicted value may drift from the authoritative one before a correction
	## triggers. A field without one inherits the entity's
	## [member PredictionComponent.divergence_epsilon]. A single scalar mixes
	## units badly for a body whose state spans meters, radians, and meters per
	## second, so the field that needs its own tolerance declares it in its own
	## units.
	## [codeblock]
	## Netw.configure_property(self, &"velocity").state().epsilon(0.05)
	## [/codeblock]
	func epsilon(threshold: float) -> PropertyConfig:
		epsilon_override = threshold
		return self


	## Excludes the field from triggering a correction, while a correction
	## another field triggers still restores it. Right for a value whose own
	## drift is tolerable but that must land with the rest of the closure when
	## one lands.
	## [codeblock]
	## Netw.configure_property(self, &"heading").state().reconcile_only()
	## [/codeblock]
	func reconcile_only() -> PropertyConfig:
		explicit_reconcile_only = true
		return self


	## Sends the whole set every eligible tick whether or not a field changed,
	## throttled to at most one send per [param interval] seconds. Writes
	## [member NetwSyncSet.trigger] for every field in the set.
	## [codeblock]
	## # position changes every tick anyway, change detection is pure overhead
	## Netw.configure_property(self, &"position").state().every_tick()
	## [/codeblock]
	func every_tick(interval: float = 0.0) -> PropertyConfig:
		_warn_double_set(set_trigger != UNSET, "trigger")
		set_trigger = NetwSyncSet.Trigger.TRIGGER_TICK
		set_every_tick_interval = interval
		return self


	## Sends the set only when a field changed since the last send. The default
	## [member NetwSyncSet.trigger] of every kind mark.
	func on_change() -> PropertyConfig:
		_warn_double_set(set_trigger != UNSET, "trigger")
		set_trigger = NetwSyncSet.Trigger.TRIGGER_ON_CHANGE
		return self


	## Re-sends the whole set every [param ticks] ticks even when nothing changed,
	## so a peer that missed an [method on_change] send converges in bounded time.
	## [codeblock]
	## # a lost "stunned" flip heals within a second at 60 ticks
	## Netw.configure_property(self, &"stunned").state().heartbeat(60)
	## [/codeblock]
	func heartbeat(ticks: int) -> PropertyConfig:
		_warn_double_set(set_heartbeat_ticks != UNSET, "heartbeat")
		set_heartbeat_ticks = ticks
		return self


	## Carries the last [param samples] ticks of values in every volatile send, so
	## a lost datagram heals from the next one's redundancy instead of a
	## retransmit round trip. Writes [member NetwSyncSet.window] for the whole set.
	## [codeblock]
	## # input() already windows at 2; 3 survives two consecutive lost datagrams
	## Netw.configure_property(self, &"motion").input().windowed(3)
	## [/codeblock]
	func windowed(samples: int) -> PropertyConfig:
		_warn_double_set(set_window != UNSET, "window")
		set_window = samples
		return self


	## Narrows the set to the server only, or back to every recipient. Writes
	## [member NetwSyncSet.audience] for the whole set. The [method input] preset
	## already narrows its set to the server.
	func audience(server_only: bool = true) -> PropertyConfig:
		set_audience = (
				NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY if server_only
				else NetwSyncSet.Audience.AUDIENCE_PUBLIC
		)
		return self


	## Sends each viewer only the fields that changed since they last confirmed,
	## instead of one shared row to everyone. Writes [member NetwSyncSet.masked]
	## for the whole set. The same values arrive, in fewer bytes.
	## [codeblock]
	## # 50 spectators, each gets only what changed for them
	## Netw.configure_property(self, &"position").broadcast().masked()
	## [/codeblock]
	func masked() -> PropertyConfig:
		set_masked = true
		return self


	## Marks this field a column [NetwPersistenceInterface] snapshots and
	## hydrates, independent of whether it also syncs.
	##
	## The database only ever sees the value the server sees.
	## [codeblock]
	## Netw.configure_persistence(self).database(db).table(&"players")
	##
	## # never syncs, a server secret the database still keeps
	## Netw.configure_property(self, &"gold").persisted()
	##
	## # synced per tick, snapshotted every 30 seconds
	## Netw.configure_property(self, &"position").state().persisted(30.0)
	##
	## # client-authored, persistable because input() delivers it to the server
	## Netw.configure_property(self, &"loadout").input().persisted()
	## [/codeblock]
	## [param interval] of [code]0.0[/code] inherits
	## [member NetwScriptModel.PersistenceConfig.default_interval].
	func persisted(interval: float = 0.0) -> PropertyConfig:
		is_persisted = true
		persist_interval = interval
		return self


	## Marks this property as spawn state. The value is captured from the
	## authority's instance when the [constant NetwFrameEnvelope.Channel.SPAWN]
	## frame is snapshotted and applied on every receiving peer while the node
	## is still orphaned, so [method Node._enter_tree] and
	## [method Node._ready] read it on every peer.
	## [codeblock]
	## func _init() -> void:
	##     Netw.configure_property(self, &"pet_name").on_spawn()
	## [/codeblock]
	func on_spawn() -> PropertyConfig:
		is_spawn_state = true
		return self


	## Property assignments are inherently local first, so
	## [method NetwScriptModel.SyncConfig.call_local] has no effect on a property
	## and warns.
	func call_local() -> SyncConfig:
		Netw.dbg.warn(
			"PropertyConfig.call_local: call_local/call_remote has no effect "
			+ "on properties since property assignments are "
			+ "inherently local first.",
			func(m): push_warning(m)
		)
		return super()


	## Property synchronization ignores the local-call axis, so
	## [method NetwScriptModel.SyncConfig.call_remote] has no effect on a property
	## and warns.
	func call_remote() -> SyncConfig:
		Netw.dbg.warn(
			"PropertyConfig.call_remote: call_local/call_remote has no effect "
			+ "on properties since property assignments are "
			+ "inherently local first.",
			func(m): push_warning(m)
		)
		return super()


	func _warn_double_set(already: bool, axis: String) -> void:
		if already:
			Netw.dbg.warn(
				"PropertyConfig: set-level %s configured multiple times" % axis,
				func(m): push_warning(m)
			)


## The connect-lifecycle face of a typed handler registered before any session
## exists, returned by [method Netw.configure_join].
##
## A join handler's first parameter is framework-owned (the [ResolvedJoin]), so
## its wire schema is the remaining parameters. [method quantize] bit-packs those
## wire arguments with a per-position [NetwQuantize] list aligned to the sliced
## schema, validated at registration through
## [method NetwScriptModel.validate_quantizers]. A null slot leaves an argument on
## the self-describing fallback.
## [codeblock]
## func _init() -> void:
##     Netw.configure_join(spawn_at) \
##         .quantize([null, NetwQuantizeBits.new().bits(4).limits(0, 8)])
##
## func spawn_at(rj: ResolvedJoin, point: StringName, team: int) -> void:
##     ...
## [/codeblock]
class ConnectConfig:
	extends RefCounted

	## The script declaring the registered handler, the source of the wire
	## schema.
	var context_script: Script = null

	## The handler method name whose parameters (after the [ResolvedJoin] slice)
	## form the wire schema.
	var context_name: StringName = &""

	## Per-argument [NetwQuantize] list aligned to the wire schema.
	var quantizers: Array = []


	## Bit-packs the wire arguments with a per-position [NetwQuantize] list,
	## parallel to the handler parameters after the [ResolvedJoin] slice.
	func quantize(p_quantizers: Variant) -> ConnectConfig:
		var new_list: Array
		if p_quantizers is Array:
			new_list = p_quantizers
		elif p_quantizers is NetwQuantize:
			new_list = [p_quantizers]
		else:
			assert(
				false,
				"quantize: Expected NetwQuantize or Array of NetwQuantize",
			)
			return self
		quantizers = new_list
		assert(
			_validate_quantizer_support(),
			"ConnectConfig: Incompatible quantizer configured.",
		)
		return self


	# The wire schema is the handler parameters after the framework-owned
	# ResolvedJoin, so quantizers align to the sliced type list.
	func _validate_quantizer_support() -> bool:
		if context_name.is_empty():
			return true
		var types := NetwScriptModel.get_method_arg_types(
			context_script,
			context_name,
		)
		if not types.is_empty():
			types = types.slice(1)
		return NetwScriptModel.validate_quantizers(
			quantizers,
			types,
			context_name,
			context_script,
		)


## Despawn policy for one entity script, registered through
## [method Netw.configure_despawn]. Applied by receiving peers when a
## [constant NetwFrameEnvelope.Channel.DESPAWN] frame removes the node.
## [codeblock]
## func _init() -> void:
##     Netw.configure_despawn(self) \
##             .before_removal(_play_death_vfx) \
##             .linger(0.5)
## [/codeblock]
class DespawnConfig:
	extends RefCounted

	## Method invoked on the entity root before removal, or empty.
	var hook_method: StringName = &""

	## Seconds the node stays in the tree as
	## [constant NetwLivenessInterface.State.LINGERING] before it is freed.
	## [code]0.0[/code] frees immediately.
	var linger_seconds: float = 0.0


	## Runs [param callable]'s method on the entity root before the node is
	## removed, for death VFX or handing children off. Stored by method name,
	## so the callable only names the method.
	func before_removal(callable: Callable) -> DespawnConfig:
		hook_method = callable.get_method()
		return self


	## Keeps the despawned node in the tree for [param seconds] while its
	## route reports [constant NetwLivenessInterface.State.LINGERING].
	func linger(seconds: float) -> DespawnConfig:
		linger_seconds = seconds
		return self


## Client-side on-ramp config for one scene root script, registered through
## [method Netw.configure_multiplayer_scene]. It carries the presentation knobs
## the detach hook reads when a native [method SceneTree.change_scene_to_file]
## converts into a [method NetwSceneInterface.request_change_path].
##
## This config is not an authorization record. Whether a client may reach a
## scene is decided server side by a [signal NetwSceneInterface.change_requested]
## listener, never by the presence of this mark. The mark only makes a raw path
## request reachable by default and gives a listener something to read through
## [method Netw.is_multiplayer_scene].
## [codeblock]
## func _init() -> void:
##     Netw.configure_multiplayer_scene(self) \
##             .on_pending(_loading_screen) \
##             .timeout(8.0)
## [/codeblock]
class SceneMarkConfig:
	extends RefCounted

	## The scene root script this config applies to.
	var context_script: Script = null

	## Method name the detach hook calls on the scene root while a captured
	## native change is pending, so the game presents its own loading UI. Its
	## return value is ignored. Empty leaves the pending window unhandled.
	var pending_method: StringName = &""

	## Seconds the client request waits before resolving
	## [constant NetwScenePromise.Result.TIMED_OUT]. [code]0.0[/code] uses
	## [constant NetwSceneInterface.DEFAULT_REQUEST_DEADLINE].
	var deadline: float = 0.0

	## When [code]true[/code], a request for this scene is deny-default, so a
	## [signal NetwSceneInterface.change_requested] listener must
	## [method SceneChangeRequest.allow] it. Set through [method gated].
	var is_gated: bool = false

	## When [code]true[/code], an admitted request for this scene replaces the
	## whole session rather than moving one participant. Set through
	## [method session_wide].
	var is_session_wide: bool = false


	## Names a side-effect method the detach hook calls while a captured native
	## change is pending, such as one that shows a loading screen. The game
	## undoes it on [signal NetwSceneInterface.native_change_settled]. Stored by
	## name so the callable only names the method on the scene root.
	func on_pending(callable: Callable) -> SceneMarkConfig:
		pending_method = callable.get_method()
		return self


	## Sets the request deadline in [param seconds] before the promise resolves
	## [constant NetwScenePromise.Result.TIMED_OUT].
	func timeout(seconds: float) -> SceneMarkConfig:
		deadline = seconds
		return self


	## Opts this scene into deny-default, so a
	## [signal NetwSceneInterface.change_requested] listener must
	## [method SceneChangeRequest.allow] every request for it. Without this, a
	## declared or marked scene admits requests by default.
	func gated() -> SceneMarkConfig:
		is_gated = true
		return self


	## Declares that an admitted request for this scene replaces the whole
	## session ([method NetwSceneInterface.change_to]) rather than moving the one
	## requester. Session-wide requests stay deny-default, so a
	## [signal NetwSceneInterface.change_requested] listener must
	## [method SceneChangeRequest.allow] them.
	func session_wide() -> SceneMarkConfig:
		is_session_wide = true
		return self


## Archetype-level persistence policy for one entity script, registered through
## [method Netw.configure_persistence]. Names the database, table, snapshot
## cadence, and hydration timing shared by every field marked
## [method NetwScriptModel.PropertyConfig.persisted].
##
## Database routing is not a property of a property, so it lives here and not on
## [NetwScriptModel.PropertyConfig]. [NetwPersistenceInterface] reads this config to build one
## [NetwPersistenceInterface.PersistenceEngine] per spawned entity.
## [codeblock]
## func _init() -> void:
##     Netw.configure_persistence(self) \
##             .database(preload("res://data/game.tres")) \
##             .table(&"players") \
##             .interval(5.0)
##     Netw.configure_property(self, &"gold").persisted()
## [/codeblock]
class PersistenceConfig:
	extends RefCounted

	## The [NetwDatabase] every flush and hydrate reads and writes. Set by
	## [method database].
	var db: NetwDatabase = null

	## The table name records are keyed under. Set by [method table].
	var table_name: StringName = &""

	## Default snapshot cadence in seconds for columns that do not override it.
	## Set by [method interval].
	var default_interval: float = 5.0

	## When true, the server hydrates the row before the entity's
	## [constant NetwFrameEnvelope.Channel.SPAWN] frame snapshots spawn state. Set
	## by [method hydrate_on_spawn].
	var hydrate_on_spawn_enabled: bool = true

	## Method name on the entity root returning the record id, or empty to fall
	## back to [member NetwEntity.entity_id] then the node name. Set by
	## [method record_id].
	var record_id_provider: StringName = &""


	## Sets the [NetwDatabase] this archetype persists to. Required.
	func database(database: NetwDatabase) -> PersistenceConfig:
		db = database
		return self


	## Sets the table records are keyed under. Required.
	func table(name: StringName) -> PersistenceConfig:
		table_name = name
		return self


	## Sets the default snapshot cadence in seconds. Defaults to [code]5.0[/code].
	func interval(seconds: float) -> PersistenceConfig:
		default_interval = seconds
		return self


	## Sets whether the server hydrates the saved row before the spawn frame is
	## snapshotted. Defaults to [code]true[/code].
	func hydrate_on_spawn(enabled: bool = true) -> PersistenceConfig:
		hydrate_on_spawn_enabled = enabled
		return self


	## Uses [param callable]'s method on the entity root to compute the record id.
	## Stored by method name, so the callable only names the method.
	func record_id(callable: Callable) -> PersistenceConfig:
		record_id_provider = callable.get_method()
		return self


class _NodeOverlay:
	extends RefCounted

	var node_ref: WeakRef
	var configs: Dictionary = { } # StringName -> SyncConfig
	var persistence: PersistenceConfig = null


class _ScriptCache:
	extends RefCounted

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


	# Walks the script method list (with base scripts) once, filling arg types
	# and arity for every declared method. Most-derived declaration wins.
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


	# Orders names by their text, and the reason it cannot just sort the names
	# it is given: sorting an array of StringName compares interning pointers,
	# not characters, so the order depends on which names a process happened to
	# intern first. An id minted from that order names one member on the peer
	# that sent it and a different member on the peer that reads it, silently,
	# because both peers hold the same set of names and only disagree on their
	# order. The [NetwEntity] component divergence hash sorts the same names as
	# String, so text order is also the order that hash already agrees on.
	static func _ordered_by_text(names: PackedStringArray) -> Array:
		names.sort()
		var out: Array = []
		for n in names:
			out.append(StringName(n))
		return out


	# Sorted @rpc method names, the source of 1-byte method ids.
	func sorted_methods() -> Array:
		if _sorted_methods_done:
			return _sorted_methods
		_sorted_methods_done = true
		var names := PackedStringArray()
		for m in rpc_config:
			if not names.has(String(m)):
				names.append(String(m))
		_sorted_methods = _ordered_by_text(names)
		return _sorted_methods


	# Sorted script-declared property names, the source of 1-byte property ids.
	# Covers every script variable so even zero-setup syncs compress.
	func sorted_properties() -> Array:
		if _sorted_props_done:
			return _sorted_props
		_sorted_props_done = true
		var names := PackedStringArray()
		var s := _script
		while s != null:
			for p in s.get_script_property_list():
				if int(p.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE:
					var n := String(p["name"])
					if not names.has(n):
						names.append(n)
			s = s.get_base_script()
		_sorted_props = _ordered_by_text(names)
		return _sorted_props


	# Sorted script-declared signal names, the source of 1-byte signal ids.
	func sorted_signals() -> Array:
		if _sorted_signals_done:
			return _sorted_signals
		_sorted_signals_done = true
		var names := PackedStringArray()
		var s := _script
		while s != null:
			for sig in s.get_script_signal_list():
				var n := String(sig["name"])
				if not names.has(n):
					names.append(n)
			s = s.get_base_script()
		_sorted_signals = _ordered_by_text(names)
		return _sorted_signals
