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
## var span = NetTrace.begin_peer("lobby_spawn", peers, {"lobby": lobby_name})
## span.step("spawners_registering")
## # ...operation...
## span.end()       # clean close
## span.fail("err") # failure close
## [/codeblock]
class_name NetTrace
extends RefCounted

## Active span stack. The most recently opened and still-open span is at the back.
static var _active: Array = []  # Array[NetSpan]

## Registered span types: label → true. Populated via [method register].
static var _registered: Dictionary = {}

## Watch settings pushed from the editor: StringName(label) → WatchMode int.
static var _watches: Dictionary = {}

enum WatchMode {
	OFF          = 0,  ## No special action.
	BREAK_ON_FAIL = 1,  ## Trigger a breakpoint when this span type fails.
	BREAK_ON_STEP = 2,  ## Trigger a breakpoint on every step of this span type.
}


## Declares a span type so it appears in the editor's Span Timeline panel.
## Call once per span type, e.g. in [code]_ready[/code] or at class scope.
static func register(span_label: String) -> void:
	_registered[StringName(span_label)] = true
	if EngineDebugger.is_active():
		EngineDebugger.send_message("networked:span_registered", [{"label": span_label}])


## Opens a new general-purpose span and pushes it onto the active stack.
## Returns a no-op [NetSpan] (empty [member NetSpan.id]) when the editor
## debugger is not active, so consumer code needs no guards.
static func begin(span_label: String, meta: Dictionary = {}) -> NetSpan:
	if not EngineDebugger.is_active():
		return NetSpan.new(&"", span_label)
	var span_id := StringName("%s_%d" % [span_label, Time.get_ticks_usec()])
	var span := NetSpan.new(span_id, span_label, meta)
	_active.append(span)
	return span


## Opens a new peer-aware span for a multiplayer operation affecting [param peers].
## Returns a no-op [NetPeerSpan] when the editor debugger is not active.
static func begin_peer(span_label: String, peers: Array[int] = [], meta: Dictionary = {}) -> NetPeerSpan:
	if not EngineDebugger.is_active():
		return NetPeerSpan.new(&"", span_label)
	var span_id := StringName("%s_%d" % [span_label, Time.get_ticks_usec()])
	var span := NetPeerSpan.new(span_id, span_label, meta)
	for peer_id: int in peers:
		span.affects(peer_id)
	_active.append(span)
	return span


## Returns the most recently opened [NetSpan] that is still [constant NetSpan.State.OPEN],
## or [code]null[/code] if no span is currently active.
static func active_span() -> NetSpan:
	var i := _active.size() - 1
	while i >= 0:
		var s: NetSpan = _active[i]
		if s.state == NetSpan.State.OPEN:
			return s
		i -= 1
	return null


## Returns the most recently opened [NetPeerSpan] that is still open, or [code]null[/code].
static func active_peer_span() -> NetPeerSpan:
	var i := _active.size() - 1
	while i >= 0:
		var s = _active[i]
		if s is NetPeerSpan and s.state == NetSpan.State.OPEN:
			return s
		i -= 1
	return null


## Removes [param span] from the active stack.
## Called automatically by [method NetSpan.end] and [method NetSpan.fail].
static func _pop_span(span: NetSpan) -> void:
	_active.erase(span)


## Clears active spans from the previous session.
## Watches and registered types are preserved across sessions.
static func reset() -> void:
	_active.clear()


## Sets the watch mode for a span type. Called from the game side when the editor
## pushes a [code]networked:set_span_watch[/code] message.
static func set_watch(span_label: String, mode: int) -> void:
	var key := StringName(span_label)
	if mode == WatchMode.OFF:
		_watches.erase(key)
	else:
		_watches[key] = mode


## Returns the [enum WatchMode] for [param span_label], or [constant WatchMode.OFF].
static func watch_mode(span_label: String) -> int:
	return _watches.get(StringName(span_label), WatchMode.OFF)
