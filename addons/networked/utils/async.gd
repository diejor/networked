## Utility class providing async/await helpers for signal-based flow control.
class_name Async
extends Object

## Awaits [param target_signal] and aborts early if [param timer] expires first.
##
## Returns [code]true[/code] if the timer fired before the signal.
## Returns [code]false[/code] if the signal fired in time.
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


## Awaits [param target_signal], [param failure_signal], or [param timer].
##
## Returns a [Dictionary] with [code]result[/code] set to
## [code]"success"[/code], [code]"failure"[/code], or [code]"timeout"[/code].
## The [code]reason[/code] value is populated only for failure.
static func timeout_or_failure(
		target_signal: Signal,
		failure_signal: Signal,
		timer: SceneTreeTimer,
) -> Dictionary:
	var resolver := _FailureResolver.new()
	resolver.setup(target_signal, failure_signal, timer)

	var result: Dictionary = await resolver.resolved
	resolver.cleanup()
	return result


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


class _FailureResolver extends RefCounted:
	signal resolved(result: Dictionary)

	var _target: Signal
	var _failure: Signal
	var _timer: SceneTreeTimer
	var _on_signal: Callable
	var _on_failure: Callable
	var _on_timeout: Callable


	func setup(target: Signal, failure: Signal, timer: SceneTreeTimer) -> void:
		_target = target
		_failure = failure
		_timer = timer

		_on_signal = func():
			resolved.emit(
				{ "result": "success", "reason": "" },
			)
		_on_failure = func(reason: Variant):
			resolved.emit(
				{ "result": "failure", "reason": reason },
			)
		_on_timeout = func():
			resolved.emit(
				{ "result": "timeout", "reason": "" },
			)

		_connect_target()
		_failure.connect(_on_failure, CONNECT_ONE_SHOT)
		_timer.timeout.connect(_on_timeout, CONNECT_ONE_SHOT)


	func cleanup() -> void:
		_disconnect_signal(_target, _on_signal)
		_disconnect_signal(_failure, _on_failure)
		if is_instance_valid(_timer) and _timer.timeout.is_connected(_on_timeout):
			_timer.timeout.disconnect(_on_timeout)


	func _connect_target() -> void:
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


	func _disconnect_signal(target: Signal, callable: Callable) -> void:
		var obj = target.get_object()
		if not is_instance_valid(obj):
			return
		for conn in target.get_connections():
			if conn.callable.get_object() == callable.get_object():
				target.disconnect(conn.callable)
