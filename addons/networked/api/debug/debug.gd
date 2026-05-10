@tool
## Access point for Networked debug functionality.
##
## [Netw.dbg] owns logging, span creation, trace sink policy, reporter
## discovery, and component handles.
## [codeblock]
## Netw.dbg.info("Player %s connected", [player_name])
## var span := Netw.dbg.peer_span(self, "spawn", peers)
## [/codeblock]
## [br][br]
## Pass log arguments as an [Array] to defer string formatting. Guard hot paths
## with [method is_level_active] before constructing expensive arguments.
## [br][br]
## Pass a lambda to [method warn] or [method error] to preserve editor
## jump-click behaviour.
## [codeblock]
## Netw.dbg.error("Critical failure", [], func(m): push_error(m))
## [/codeblock]
class_name NetwDbg
extends RefCounted


## Emitted when the editor requests all instances to re-calculate their tiling.
signal tiling_requested

var _reporter_ref: WeakRef
var _debug_enabled := false


## Creates a [NetwHandle] for O(1) debug access from a [NetwComponent] or [Object].
func handle(context: Object) -> NetwHandle:
	return NetwHandle.new(context)


## Logs an [code]INFO[/code] message.
func info(
	arg1: Variant,
	arg2: Variant = null,
	arg3: Variant = null,
	arg4: Variant = null
) -> void:
	_log(NetwLog.Level.INFO, arg1, arg2, arg3, arg4)


## Logs a [code]DEBUG[/code] message.
func debug(
	arg1: Variant,
	arg2: Variant = null,
	arg3: Variant = null,
	arg4: Variant = null
) -> void:
	_log(NetwLog.Level.DEBUG, arg1, arg2, arg3, arg4)


## Logs a [code]TRACE[/code] message.
func trace(
	arg1: Variant,
	arg2: Variant = null,
	arg3: Variant = null,
	arg4: Variant = null
) -> void:
	_log(NetwLog.Level.TRACE, arg1, arg2, arg3, arg4)


## Logs a [code]WARN[/code] message and calls [method @GlobalScope.push_warning].
func warn(
	arg1: Variant,
	arg2: Variant = null,
	arg3: Variant = null,
	arg4: Variant = null
) -> void:
	_log(NetwLog.Level.WARN, arg1, arg2, arg3, arg4)


## Logs an [code]ERROR[/code] message and calls [method @GlobalScope.push_error].
func error(
	arg1: Variant,
	arg2: Variant = null,
	arg3: Variant = null,
	arg4: Variant = null
) -> void:
	_log(NetwLog.Level.ERROR, arg1, arg2, arg3, arg4)


## Opens a new general-purpose [NetSpan].
## [br][br]
## [param context] is the object originating the span.
## [param label] is the display name for the span.
## [param meta] is optional metadata.
## [param follows_from] is an optional causal link to a previous checkpoint.
func span(
	context: Object,
	label: String,
	meta: Dictionary = {},
	follows_from: CheckpointToken = null
) -> NetSpan:
	return NetTrace.begin(label, context, meta, "", follows_from)


## Opens a new peer-aware [NetPeerSpan].
## [br][br]
## [param context] is the object originating the span.
## [param label] is the display name for the span.
## [param peers] is an array of peer IDs involved.
## [param meta] is optional metadata.
## [param token] is an optional causal link to a previous checkpoint.
func peer_span(
	context: Object,
	label: String,
	peers: Array = [],
	meta: Dictionary = {},
	token: CheckpointToken = null
) -> NetPeerSpan:
	return NetTrace.begin_peer(label, peers, context, meta, "", token)


## Returns the currently active [NetSpan] on the top of the trace stack.
func active_span() -> NetSpan:
	return NetTrace.active_span()


## Resets the [NetTrace] system, clearing all active spans.
func reset() -> void:
	NetTrace.reset()


## Returns [code]true[/code] when debug reporter features are enabled.
func is_enabled() -> bool:
	return _debug_enabled


func _set_enabled(enabled: bool) -> void:
	_debug_enabled = enabled


## Returns [code]true[/code] when [param level] is active for [param path].
func is_level_active(level: int, path: String = "") -> bool:
	if path.is_empty():
		NetwLog._ensure_initialized()
		return level >= NetwLog._effective_min_level
	return NetwLog.is_level_active(level, path)


## Returns the active debug reporter or [code]null[/code].
func get_reporter() -> Node:
	if not _reporter_ref:
		return null
	var reporter := _reporter_ref.get_ref() as Node
	return reporter if is_instance_valid(reporter) else null


## Registers [param reporter] as the current debug reporter endpoint.
func register_reporter(reporter: Node) -> void:
	if is_instance_valid(reporter):
		_reporter_ref = weakref(reporter)


## Clears [param reporter] if it owns the current reporter endpoint.
func unregister_reporter(reporter: Node) -> void:
	if get_reporter() == reporter:
		_reporter_ref = null


## Registers [param mt] with the active reporter if one is available.
func register_tree(mt: MultiplayerTree) -> void:
	var reporter := get_reporter()
	if reporter and reporter.has_method(&"register_tree"):
		reporter.register_tree(mt)


## Unregisters [param mt] from the active reporter if one is available.
func unregister_tree(mt: MultiplayerTree) -> void:
	var reporter := get_reporter()
	if reporter and reporter.has_method(&"unregister_tree"):
		reporter.unregister_tree(mt)


## Installs [param sink] as the active trace telemetry sink.
func install_trace_sink(sink: Callable) -> void:
	NetTrace.message_delegate = sink


## Clears the active trace telemetry sink.
func clear_trace_sink(expected_sink: Callable = Callable()) -> void:
	if expected_sink.is_valid() and NetTrace.message_delegate != expected_sink:
		return
	NetTrace.message_delegate = Callable()


## Enables reporter-backed tracing until the returned scope is closed.
func enable_for_test() -> NetwDbgScope:
	var scope := NetwDbgScope.new(_debug_enabled, NetTrace.message_delegate)
	_debug_enabled = true
	var reporter := get_reporter()
	if reporter and reporter.has_method(&"set_enabled"):
		reporter.set_enabled(true)
	return scope


func _close_scope(previous_enabled: bool, previous_sink: Callable) -> void:
	_debug_enabled = previous_enabled
	NetTrace.message_delegate = previous_sink
	if not _debug_enabled:
		var reporter := get_reporter()
		if reporter and reporter.get_parent() \
				and reporter.get_parent().has_method(&"set_enabled"):
			reporter.get_parent().set_enabled(false)
		elif reporter and reporter.has_method(&"set_enabled"):
			reporter.set_enabled(false)
		NetTrace.reset()


func _log(
	level: int,
	arg1: Variant,
	arg2: Variant,
	arg3: Variant,
	arg4: Variant
) -> void:
	NetwLog._ensure_initialized()
	if level < NetwLog._effective_min_level:
		return

	var context: Object = null
	var msg: Variant = ""
	var args: Array = []
	var link_call: Callable = Callable()

	if typeof(arg1) == TYPE_OBJECT and not arg1 is String and \
			not arg1 is StringName:
		context = arg1
		msg = arg2
		if typeof(arg3) == TYPE_ARRAY:
			args = arg3
			if typeof(arg4) == TYPE_CALLABLE:
				link_call = arg4
		elif typeof(arg3) == TYPE_CALLABLE:
			link_call = arg3
	else:
		msg = arg1
		if typeof(arg2) == TYPE_ARRAY:
			args = arg2
			if typeof(arg3) == TYPE_CALLABLE:
				link_call = arg3
		elif typeof(arg2) == TYPE_CALLABLE:
			link_call = arg2

	if context:
		var script := context.get_script() as Script
		if script and not NetwLog.is_level_active(level, script.resource_path):
			return
		elif not script and not NetwLog.is_level_active_for_module(
			level,
			context.get_class()
		):
			return

		var component := context as Node
		if component:
			var peer_id := -1
			if component.is_inside_tree() and component.multiplayer:
				peer_id = component.multiplayer.get_unique_id()

			var peer_label := "S" if peer_id == 1 else \
				"C%d" % peer_id if peer_id > 0 else "?"
			var owner_name := component.owner.name if component.owner else \
				component.name

			var cls_name: String = ""
			if script:
				cls_name = script.resource_path.get_file().get_basename()
			else:
				cls_name = component.get_class()

			msg = "[%s] [%s] [%s] %s" % [peer_label, owner_name, cls_name, str(msg)]

	match level:
		NetwLog.Level.TRACE: NetwLog.trace(msg, args)
		NetwLog.Level.DEBUG: NetwLog.debug(msg, args)
		NetwLog.Level.INFO: NetwLog.info(msg, args)
		NetwLog.Level.WARN: NetwLog.warn(msg, args, link_call)
		NetwLog.Level.ERROR: NetwLog.error(msg, args, link_call)
