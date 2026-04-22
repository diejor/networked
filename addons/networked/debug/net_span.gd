## Cross-frame call stack entry for the Networked debugger.
##
## Tracks a named operation across multiple process frames, recording a trail
## of named checkpoints and a final outcome (clean close or failure).
##
## Do not instantiate directly, use [method NetTrace.begin] or 
## [method NetTrace.begin_peer].
class_name NetSpan
extends RefCounted

enum State { OPEN, CLOSED, FAILED }

## Unique identifier for this span. Empty on no-op spans (debugger inactive).
var id: StringName

## Human-readable label for this span type (e.g., [code]"lobby_spawn"[/code]).
var label: String

## The name of the [MultiplayerTree] this span belongs to. Empty for global spans.
var tree_name: String

## Current lifecycle state.
var state: State = State.OPEN

var _start_frame: int
var _start_usec: int

## Ordered step trail recorded via [method step].
var _steps: Array = []

## Weak reference to the tree owning this span. Used for O(1) routing.
var _mt: WeakRef

## Metadata provided at creation.
var meta: Dictionary

## Explicit target node for this span, set via [method with_node].
var _target_node: WeakRef


func _init(p_id: StringName, p_label: String, p_meta: Dictionary = {}, tree: MultiplayerTree = null, p_tree_name: String = "", follows_from: CheckpointToken = null) -> void:
	id = p_id
	label = p_label
	meta = p_meta
	tree_name = p_tree_name
	if is_instance_valid(tree):
		_mt = weakref(tree)
		if tree_name.is_empty():
			tree_name = tree.get_meta(&"_original_name", tree.name)
	
	_start_frame = Engine.get_process_frames()
	_start_usec = Time.get_ticks_usec()
	if id.is_empty():
		return
	_send("networked:span_open", {
		"id": str(id),
		"label": label,
		"tree_name": tree_name,
		"frame": _start_frame,
		"timestamp_usec": _start_usec,
		"meta": meta,
		"affected_peers": _get_affected_peers(),
		"caller": _get_caller(),
		"follows_from": follows_from.to_dict() if follows_from else {},
	})


## Attaches an explicit target node to this span for diagnostic snapshots.
## Returns [code]self[/code] for method chaining.
func with_node(node: Node) -> NetSpan:
	_target_node = weakref(node) if is_instance_valid(node) else null
	return self


## Returns the target node if it is still valid, otherwise [code]null[/code].
func get_target_node() -> Node:
	return _target_node.get_ref() if _target_node else null


## Records a named checkpoint in this span's step trail and sends it to the editor.
## Returns [code]self[/code] for method chaining:
## [codeblock]
## span.step("visibility_set").step("clients_notified")
## [/codeblock]
func step(step_label: String, data: Dictionary = {}) -> NetSpan:
	if state != State.OPEN or id.is_empty():
		return self
	var s := {
		"label": step_label,
		"data": data,
		"frame": Engine.get_process_frames(),
		"usec": Time.get_ticks_usec(),
		"caller": _get_caller(),
	}
	_steps.append(s)
	_send("networked:span_step", {
		"id": str(id),
		"span_label": label,
		"step": s,
		"step_index": _steps.size() - 1,
	})
	return self


## Records a non-fatal warning checkpoint within this span without closing it.
##
## Use this for conditions that are suspicious but not immediately fatal —
## the span stays OPEN for further steps. For a fatal outcome, use [method fail].
## Sends [code]networked:span_step_warn[/code] and emits [method push_warning].
func step_warn(step_label: String, message: String = "", data: Dictionary = {}) -> NetSpan:
	if state != State.OPEN or id.is_empty():
		return self
	var s := {
		"label": step_label,
		"message": message,
		"data": data,
		"frame": Engine.get_process_frames(),
		"usec": Time.get_ticks_usec(),
		"caller": _get_caller(),
	}
	_steps.append(s)
	_send("networked:span_step_warn", {
		"id": str(id),
		"span_label": label,
		"step": s,
		"step_index": _steps.size() - 1,
	})
	return self


## Closes the span with a successful outcome.
func end() -> void:
	if state != State.OPEN or id.is_empty():
		return
	state = State.CLOSED
	NetTrace._pop_span(self)
	_send("networked:span_close", {
		"id": str(id),
		"label": label,
		"outcome": "ok",
		"frame": Engine.get_process_frames(),
		"elapsed_usec": Time.get_ticks_usec() - _start_usec,
	})


## Closes the span with a failure outcome and forwards context to the editor.
## [param reason] is a short machine-readable tag, e.g. [code]"simplify_path_race"[/code].
## [param data] is arbitrary serialisable context attached to the failure.
func fail(reason: String, data: Dictionary = {}) -> void:
	if state != State.OPEN or id.is_empty():
		return
	state = State.FAILED
	NetTrace._pop_span(self)
	_send("networked:span_fail", {
		"id": str(id),
		"label": label,
		"reason": reason,
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"elapsed_usec": Time.get_ticks_usec() - _start_usec,
		"steps": _steps,
		"affected_peers": _get_affected_peers(),
		"data": data,
		"caller": _get_caller(),
	})


## Opens a new phase scope within this span. 
## Returns a [NetSpanPhase] that should be closed with [method NetSpanPhase.done].
func phase(phase_name: String) -> NetSpanPhase:
	return NetSpanPhase.new(self, phase_name)


## Captures the current span state as a [CheckpointToken] for causal linking.
##
## Pass the returned token to [method NetTrace.begin] or [method NetTrace.begin_peer]
## via the [param follows_from] parameter to declare an explicit causal relationship
## between this span and the new one.
## [param step_label] is optional — use it when the token represents a specific
## step within this span rather than the span as a whole.
func checkpoint(step_label: String = "") -> CheckpointToken:
	var t := CheckpointToken.new()
	t.span_id = id
	t.span_label = label
	t.step_label = step_label
	t.frame = Engine.get_process_frames()
	t.usec = Time.get_ticks_usec()
	return t


## Returns the affected peer IDs. Empty for base [NetSpan]; overridden by [NetPeerSpan].
func _get_affected_peers() -> Array[int]:
	return []


## Returns the first call-stack frame whose source is outside the networked addon.
## This is the user's call site, the line where [method NetTrace.begin] or [method step]
## was invoked. Returns an empty dict in release builds ([method get_stack] returns 
## [code][][/code]).
static func _get_caller() -> Dictionary:
	for frame: Dictionary in get_stack():
		var src := frame.get("source", "") as String
		# Skip only the span infrastructure and the base NetComponent helper.
		# Frames from the reporter or user code are the meaningful call site.
		if src.contains("addons/networked/debug/net_span.gd") \
				or src.contains("addons/networked/debug/net_peer_span.gd") \
				or src.contains("addons/networked/debug/net_trace.gd") \
				or src.contains("addons/networked/components/net_component.gd") \
				or src.contains("addons/networked/core/netw_dbg.gd") \
				or src.contains("addons/networked/core/netw_handle.gd"):
			continue
		return frame
	return {}


func _send(msg: String, payload: Dictionary) -> void:
	if NetTrace.message_delegate.is_valid():
		var mt: MultiplayerTree = _mt.get_ref() if _mt else null
		NetTrace.message_delegate.call(msg, payload, mt)


## Scope object for a named phase within a [NetSpan].
class NetSpanPhase extends RefCounted:
	var _span: NetSpan
	var _name: String

	func _init(span: NetSpan, name: String) -> void:
		_span = span
		_name = name
		_span.step(_name + "_begin")

	## Records the end of the phase as a step.
	func done() -> void:
		_span.step(_name + "_end")
