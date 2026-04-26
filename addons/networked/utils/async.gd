## Utility class providing async/await helpers for signal-based flow control.
class_name Async
extends Object


## Awaits [param target_signal] and aborts early if [param timer] expires first.
##
## Returns [code]true[/code] if the timer fired before the signal, [code]false[/code] if the signal fired in time.
## [codeblock]
## var timer := get_tree().create_timer(5.0)
## if await Async.timeout(my_signal, timer):
##     push_error("Timed out waiting for my_signal.")
## [/codeblock]
static func timeout(target_signal: Signal, timer: SceneTreeTimer) -> bool:
	var dummy := RefCounted.new()
	dummy.add_user_signal("resolved")
	var resolved_signal := Signal(dummy, "resolved")
	
	var on_signal = func(_a1=null, _a2=null, _a3=null, _a4=null, _a5=null):
		resolved_signal.emit(false)
	
	var on_timeout = func():
		resolved_signal.emit(true)
	
	target_signal.connect(on_signal, CONNECT_ONE_SHOT)
	timer.timeout.connect(on_timeout, CONNECT_ONE_SHOT)
	
	assert(timer.time_left > 0.0, "Timer already fired before await.")
	var did_timeout: bool = await resolved_signal
	
	if did_timeout:
		var obj := target_signal.get_object()
		if is_instance_valid(obj) and target_signal.is_connected(on_signal):
			target_signal.disconnect(on_signal)
	
	return did_timeout
