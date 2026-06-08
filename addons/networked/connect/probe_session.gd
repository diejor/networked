## One probe-in-flight against a [JoinTarget].
##
## Thin wrapper around [method BackendPeer.query_server_info]: builds a
## fresh backend via [method JoinTarget.make_backend_instance], awaits
## the result, and emits [signal completed].
## [br][br]
## [b]Cancellation:[/b] [method cancel] suppresses the [signal completed]
## emission only. [method BackendPeer.query_server_info] owns its
## transient peer and handles teardown on its own
## timeout/completion path. This wrapper cannot close the inner peer
## early.
class_name ProbeSession
extends RefCounted

## Emitted exactly once per [method run] call, unless [method cancel]
## was invoked first.
signal completed(result: ServerInfoResult)

var target: JoinTarget
var timeout: float

var _cancelled: bool = false
var _finished: bool = false


func _init(
		p_target: JoinTarget,
		p_timeout: float = 2.0,
) -> void:
	target = p_target
	timeout = p_timeout


## Runs the probe and emits [signal completed] with the
## [ServerInfoResult]. Returns the same result so callers can [code]
## await session.run()[/code] without wiring the signal.
func run() -> ServerInfoResult:
	if target == null:
		var bad := ServerInfoResult.error("ProbeSession: null target")
		_emit(bad)
		return bad

	var backend := target.make_backend_instance()
	if backend == null:
		var bad := ServerInfoResult.error("ProbeSession: target has no backend")
		_emit(bad)
		return bad

	var result: ServerInfoResult = await backend.query_server_info(
		target.address,
		timeout,
	)
	_emit(result)
	return result


## Marks the session cancelled so a late [signal completed] is not
## emitted. The inner [method BackendPeer.query_server_info] still
## runs to completion and tears down its transient peer.
func cancel() -> void:
	_cancelled = true


## Returns [code]true[/code] once [method run] has resolved (or
## [method cancel] suppressed it).
func is_finished() -> bool:
	return _finished


func _emit(result: ServerInfoResult) -> void:
	_finished = true
	if _cancelled:
		return
	completed.emit(result)
