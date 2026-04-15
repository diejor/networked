## Cross-frame call stack entry for the Networked debugger.
##
## Tracks a named operation across multiple process frames, recording a trail
## of named checkpoints and a final outcome (clean close or failure).
##
## Do not instantiate directly — use [NetTrace.begin] or [NetTrace.begin_peer].
class_name NetSpan
extends RefCounted

enum State { OPEN, CLOSED, FAILED }

## Unique identifier for this span. Doubles as the [code]correlation_id[/code]
## visible in the Log Bridge panel. Empty on no-op spans (debugger inactive).
var id: StringName

## Human-readable label for this span type (e.g., [code]"lobby_spawn"[/code]).
var label: String

## Current lifecycle state.
var state: State = State.OPEN

var _start_frame: int
var _start_usec: int

## Ordered step trail recorded via [method step].
var _steps: Array = []


func _init(p_id: StringName, p_label: String, meta: Dictionary = {}) -> void:
	id = p_id
	label = p_label
	_start_frame = Engine.get_process_frames()
	_start_usec = Time.get_ticks_usec()
	if id.is_empty():
		return
	_send("networked:span_open", {
		"id": str(id),
		"label": label,
		"frame": _start_frame,
		"timestamp_usec": _start_usec,
		"meta": meta,
		"affected_peers": _get_affected_peers(),
	})


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
	}
	_steps.append(s)
	_send("networked:span_step", {
		"id": str(id),
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
	})


## Returns the affected peer IDs. Empty for base [NetSpan]; overridden by [NetPeerSpan].
func _get_affected_peers() -> Array[int]:
	return []


func _send(msg: String, payload: Dictionary) -> void:
	if EngineDebugger.is_active():
		EngineDebugger.send_message(msg, [payload])
