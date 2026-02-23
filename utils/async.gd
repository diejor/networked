class_name Async
extends Object

## Awaits a signal but aborts if the timeout is reached first.
## Returns `true` if it timed out, and `false` if the signal fired in time.
static func timeout(target_signal: Signal, timer: SceneTreeTimer) -> bool:
	var dummy = RefCounted.new()
	dummy.add_user_signal("resolved")
	var resolved_signal = Signal(dummy, "resolved")
	
	var on_signal = func(arg1=null, arg2=null, arg3=null, arg4=null, arg5=null):
		resolved_signal.emit(false) # did NOT timeout
		
	var on_timeout = func():
		resolved_signal.emit(true)  # DID timeout
	
	target_signal.connect(on_signal, CONNECT_ONE_SHOT)
	timer.timeout.connect(on_timeout, CONNECT_ONE_SHOT)
	
	assert(timer.time_left > 0., "Timer already fired before await.")
	var did_timeout = await resolved_signal
	
	if did_timeout and target_signal.is_connected(on_signal):
		target_signal.disconnect(on_signal)
		
	return did_timeout
