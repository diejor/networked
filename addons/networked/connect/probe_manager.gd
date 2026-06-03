## Caps concurrent [ProbeSession]s so a single browser instance does
## not flood the server.
##
## [member max_concurrent] sits below the server-side
## [code]MAX_ACTIVE_PROBES[/code] (32 in [AuthProbeResponder]) so a single
## client cannot self-throttle the host. [member default_timeout] stays
## below [member SceneMultiplayer.auth_timeout] (3.0s) to avoid racing
## the server-side reaper.
## [br][br]
## Submit work via [method query], and cancel everything in flight with
## [method cancel_all]. BUSY replies are propagated unchanged - the UI
## decides whether to retry or render "busy".
class_name ProbeManager
extends Node

## Maximum number of [ProbeSession]s allowed to run at once.
@export_range(1, 32, 1) var max_concurrent: int = 6

## Default per-probe timeout in seconds. Must stay below
## [code]SceneMultiplayer.auth_timeout[/code] (3.0s).
@export_range(0.1, 5.0, 0.1, "suffix:s") var default_timeout: float = 2.0


class _PendingQuery:
	var target: JoinTarget
	var on_done: Callable
	var timeout: float


var _active: Array[ProbeSession] = []
var _queue: Array[_PendingQuery] = []


func _init() -> void:
	name = "ProbeManager"


## Enqueues a probe for [param target]. [param on_done] is called with
## the [ServerInfoResult]. If the active count is below
## [member max_concurrent], the probe starts immediately. Otherwise it
## waits until a slot frees.
##
## [param timeout] overrides [member default_timeout] when > 0.
func query(
		target: JoinTarget,
		on_done: Callable,
		timeout: float = -1.0,
) -> void:
	var pending := _PendingQuery.new()
	pending.target = target
	pending.on_done = on_done
	pending.timeout = timeout if timeout > 0.0 else default_timeout
	_queue.append(pending)
	_pump()


## Cancels every in-flight session and clears the pending queue.
## Cancelled sessions will not fire queued completion callbacks.
func cancel_all() -> void:
	_queue.clear()
	for session in _active:
		session.cancel()


## Number of probes currently running.
func active_count() -> int:
	return _active.size()


## Number of probes waiting for a slot.
func queued_count() -> int:
	return _queue.size()


func _pump() -> void:
	while _queue.size() > 0 and _active.size() < max_concurrent:
		var pending: _PendingQuery = _queue.pop_front()
		_start(pending)


func _start(pending: _PendingQuery) -> void:
	var session := ProbeSession.new(pending.target, pending.timeout)
	_active.append(session)
	_run_session(session, pending.on_done)


func _run_session(session: ProbeSession, on_done: Callable) -> void:
	var result: ServerInfoResult = await session.run()
	_active.erase(session)
	if not session._cancelled and on_done.is_valid():
		on_done.call(result)
	_pump()
