## Static access point for the Networked span tracing system.
##
## Opens cross-frame call stacks ([NetSpan] / [NetPeerSpan]) and tracks which
## span is currently active so [NetworkedDebugReporter] can attach C++ errors
## to the right operation.
##
## All state lives in static variables that persist for the process lifetime.
## [NetworkedDebugReporter] calls [method reset] at the start of each debug session
## to clear any stale spans from a previous run.
##
## [b]Usage:[/b]
## [codeblock]
## var span = NetTrace.begin_peer("lobby_spawn", peers, self)
## span.step("spawners_registering")
## # ...operation...
## span.end()       # clean close
## span.fail("err") # failure close
## [/codeblock]
class_name NetTrace
extends RefCounted

## Static delegate for sending telemetry messages to a debugger backend.
## If not set, all trace operations are silent no-ops.
## Contract: (msg: String, payload: Dictionary, mt: MultiplayerTree)
static var message_delegate: Callable


## Active span stack. The most recently opened and still-open span is at the back.
static var _active: Array = []  # Array[RefCounted]


## Opens a new general-purpose span and pushes it onto the active stack.
## Returns a no-op [NetSpan] (empty [member NetSpan.id]) when the debugger
## is not active, so consumer code needs no guards.
## Pass [param context] (typically [code]self[/code]) to automatically 
## route the span to the correct MultiplayerTree.
static func begin(span_label: String, context: Object = null, meta: Dictionary = {}, tree_name: String = "", follows_from: CheckpointToken = null) -> NetSpan:
	if not message_delegate.is_valid():
		return NetSpan.new(&"", span_label)

	var mt := MultiplayerTree.resolve(context)
	if mt and tree_name.is_empty():
		tree_name = mt.get_meta(&"_original_name", mt.name)

	var span_id := StringName("%s_%d" % [span_label, Time.get_ticks_usec()])
	var span := NetSpan.new(span_id, span_label, meta, mt, follows_from)
	_active.append(span)
	return span


## Opens a new peer-aware span for a multiplayer operation affecting [param peers].
## Returns a no-op [NetPeerSpan] when the debugger is not active.
static func begin_peer(span_label: String, peers: Array = [], context: Object = null, meta: Dictionary = {}, tree_name: String = "", follows_from: CheckpointToken = null) -> NetPeerSpan:
	if not message_delegate.is_valid():
		return NetPeerSpan.new(&"", span_label)

	var mt := MultiplayerTree.resolve(context)
	if mt and tree_name.is_empty():
		tree_name = mt.get_meta(&"_original_name", mt.name)

	var span_id := StringName("%s_%d" % [span_label, Time.get_ticks_usec()])
	var span := NetPeerSpan.new(span_id, span_label, meta, mt, follows_from)
	for peer_id: int in peers:
		span.affects(peer_id)
	
	_active.append(span)
	return span


## Returns the most recently opened span that is still OPEN,
## or [code]null[/code] if no span is currently active.
static func active_span() -> RefCounted:
	var i := _active.size() - 1
	while i >= 0:
		var s = _active[i]
		if s.state == 0: # NetSpan.State.OPEN
			return s
		i -= 1
	return null


## Returns the most recently opened peer-aware span that is still open, or [code]null[/code].
static func active_peer_span() -> RefCounted:
	var i := _active.size() - 1
	while i >= 0:
		var s = _active[i]
		if s.has_method(&"affects") and s.state == 0: # NetSpan.State.OPEN
			return s
		i -= 1
	return null


## Removes [param span] from the active stack.
## Called automatically by [method NetSpan.end] and [method NetSpan.fail].
static func _pop_span(span: RefCounted) -> void:
	_active.erase(span)


## Clears active spans from the previous session.
static func reset() -> void:
	_active.clear()
