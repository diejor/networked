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
	var resolver := _Resolver.new()
	resolver.setup(target_signal, timer)
	
	var did_timeout: bool = await resolver.resolved
	resolver.cleanup()
	return did_timeout


class _Resolver extends RefCounted:
	signal resolved(is_timeout: bool)

	var _target: Signal
	var _timer: SceneTreeTimer
	var _on_signal: Callable
	var _on_timeout: Callable

	func setup(target: Signal, timer: SceneTreeTimer) -> void:
		_target = target
		_timer = timer

		_on_signal = func(): resolved.emit(false)
		_on_timeout = func(): resolved.emit(true)

		var count := 0
		var signals := _target.get_object().get_signal_list()
		for s in signals:
			if s.name == _target.get_name():
				count = s.args.size()
				break

		if count > 0:
			_target.connect(_on_signal.unbind(count), CONNECT_ONE_SHOT)
		else:
			_target.connect(_on_signal, CONNECT_ONE_SHOT)

		_timer.timeout.connect(_on_timeout, CONNECT_ONE_SHOT)

	func cleanup() -> void:
		var obj = _target.get_object()
		if is_instance_valid(obj):
			for conn in _target.get_connections():
				if conn.callable.get_object() == _on_signal.get_object():
					_target.disconnect(conn.callable)

		if is_instance_valid(_timer) and _timer.timeout.is_connected(_on_timeout):
			_timer.timeout.disconnect(_on_timeout)
