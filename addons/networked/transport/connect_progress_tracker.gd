class_name ConnectProgressTracker
extends RefCounted
## Smoothly eases connection progress bar values during client join attempts.
##
## [MultiplayerTree] or [BackendPeer] drives this tracker during a client connection
## to provide a smooth, time-eased percentage to the connection UI instead of
## freezing at static thresholds.
##
## [codeblock]
## var tracker := ConnectProgressTracker.new()
## tracker.start(Time.get_ticks_msec(), 10.0)
## # ...in poll loop...
## var sample := tracker.poll(Time.get_ticks_msec())
## if not sample.is_empty():
##     print("Progress: ", sample.message, " ratio: ", sample.ratio)
## [/codeblock]

## The exponential ease factor used to calculate the progress curve.
const EASE := 5.0

## The maximum ratio limit returned by this tracker to reserve the final tick for success.
const MAX_RATIO := 0.98

## Throttling interval in milliseconds for regular poll updates.
const EMIT_INTERVAL_MS := 100

var _start_ms := 0
var _bound := 0.0
var _message := ""
var _step := &""
var _last_emit_ms := 0


## Starts tracking progress with the starting timestamp [param start_ms] and timeout budget [param bound].
##
## The [param bound] determines the scaling factor for the eased progress ratio.
## [codeblock]
## tracker.start(Time.get_ticks_msec(), 10.0)
## [/codeblock]
func start(start_ms: int, bound: float) -> void:
	_start_ms = start_ms
	_bound = maxf(bound, 0.1)
	_message = ""
	_step = &""
	_last_emit_ms = 0


## Resets the tracker state to idle.
##
## Clears the starting timestamp and all active progress messages/steps.
## [codeblock]
## tracker.stop()
## [/codeblock]
func stop() -> void:
	_start_ms = 0
	_bound = 0.0
	_message = ""
	_step = &""
	_last_emit_ms = 0


## Sets the current progress [param message] and returns the updated sample.
##
## Bypasses the polling throttle interval to immediately return the updated status.
## [codeblock]
## var sample := tracker.set_message("Handshaking...", Time.get_ticks_msec())
## [/codeblock]
func set_message(message: String, now_ms: int) -> Dictionary:
	_message = message
	return _sample(now_ms, true)


## Sets the current progress [param step] and returns the updated sample.
##
## Bypasses the polling throttle interval to immediately return the updated status.
## [codeblock]
## var sample := tracker.set_step(&"handshake", Time.get_ticks_msec())
## [/codeblock]
func set_step(step: StringName, now_ms: int) -> Dictionary:
	_step = step
	return _sample(now_ms, true)


## Polls the current tracker state and returns the sample.
##
## Throttles emissions to [constant EMIT_INTERVAL_MS] (100ms) unless forced. Returns
## an empty [Dictionary] when throttled or idle.
## [codeblock]
## var sample := tracker.poll(Time.get_ticks_msec())
## [/codeblock]
func poll(now_ms: int) -> Dictionary:
	return _sample(now_ms, false)


## Returns the current time-eased ratio.
##
## The ratio is eased using the exponent of [constant EASE] and clamped between
## [code]0.0[/code] and [constant MAX_RATIO].
## [codeblock]
## var val := tracker.ratio(Time.get_ticks_msec())
## [/codeblock]
func ratio(now_ms: int) -> float:
	if _start_ms <= 0 or _bound <= 0.0:
		return 0.0
	var elapsed := float(now_ms - _start_ms) * 0.001
	var scaled := maxf(0.0, elapsed / _bound)
	return clampf(1.0 - exp(-EASE * scaled), 0.0, MAX_RATIO)


func _sample(now_ms: int, force: bool) -> Dictionary:
	if _start_ms <= 0 or (_message.is_empty() and _step.is_empty()):
		return { }
	if not force and now_ms - _last_emit_ms < EMIT_INTERVAL_MS:
		return { }
	_last_emit_ms = now_ms
	return {
		"step": _step,
		"message": _message,
		"ratio": ratio(now_ms),
	}
