class_name Async
extends Object

## Awaits a signal but aborts if the timeout is reached first.
## Returns `true` if it timed out, and `false` if the signal fired in time.
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
