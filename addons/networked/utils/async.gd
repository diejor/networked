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


# Counts the argument signature of [param sig] so unbound connections match.
static func _signal_arg_count(sig: Signal) -> int:
	var obj := sig.get_object()
	if not is_instance_valid(obj):
		return 0
	for s in obj.get_signal_list():
		if s.name == sig.get_name():
			return (s.args as Array).size()
	return 0


# Disconnects every connection on [param sig] whose callable is bound to
# [param owner], tolerating an already-freed signal source.
static func _disconnect_owned(sig: Signal, owner: Object) -> void:
	if not is_instance_valid(sig.get_object()):
		return
	for conn in sig.get_connections():
		if conn.callable.get_object() == owner:
			sig.disconnect(conn.callable)


class _Resolver extends RefCounted:
	signal resolved(is_timeout: bool)

	var _target: Signal
	var _timer: SceneTreeTimer


	func setup(target: Signal, timer: SceneTreeTimer) -> void:
		_target = target
		_timer = timer

		var count := Async._signal_arg_count(_target)
		if count > 0:
			_target.connect(_on_signal.unbind(count), CONNECT_ONE_SHOT)
		else:
			_target.connect(_on_signal, CONNECT_ONE_SHOT)

		_timer.timeout.connect(_on_timeout, CONNECT_ONE_SHOT)


	func cleanup() -> void:
		Async._disconnect_owned(_target, self)
		if is_instance_valid(_timer) and _timer.timeout.is_connected(_on_timeout):
			_timer.timeout.disconnect(_on_timeout)
		_target = Signal()
		_timer = null


	func _on_signal() -> void:
		resolved.emit(false)


	func _on_timeout() -> void:
		resolved.emit(true)


class _FailureResolver extends RefCounted:
	signal resolved(result: Dictionary)

	var _target: Signal
	var _failure: Signal
	var _timer: SceneTreeTimer


	func setup(target: Signal, failure: Signal, timer: SceneTreeTimer) -> void:
		_target = target
		_failure = failure
		_timer = timer

		var count := Async._signal_arg_count(_target)
		if count > 0:
			_target.connect(_on_signal.unbind(count), CONNECT_ONE_SHOT)
		else:
			_target.connect(_on_signal, CONNECT_ONE_SHOT)

		_failure.connect(_on_failure, CONNECT_ONE_SHOT)
		_timer.timeout.connect(_on_timeout, CONNECT_ONE_SHOT)


	func cleanup() -> void:
		Async._disconnect_owned(_target, self)
		Async._disconnect_owned(_failure, self)
		if is_instance_valid(_timer) and _timer.timeout.is_connected(_on_timeout):
			_timer.timeout.disconnect(_on_timeout)
		_target = Signal()
		_failure = Signal()
		_timer = null


	func _on_signal() -> void:
		resolved.emit({ "result": "success", "reason": "" })


	func _on_failure(reason: Variant) -> void:
		resolved.emit({ "result": "failure", "reason": reason })


	func _on_timeout() -> void:
		resolved.emit({ "result": "timeout", "reason": "" })
