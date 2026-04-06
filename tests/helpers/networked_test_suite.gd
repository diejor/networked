## Base class for networked tests that provides timeout-safe await helpers.
class_name NetworkedTestSuite
extends GdUnitTestSuite

const DEFAULT_TIMEOUT := 1.0


## Awaits a signal and fails the test if it times out.
## [codeblock]
## await timeout_await(my_signal, 1.0)
## [/codeblock]
func timeout_await(target_signal: Signal, timeout: float = DEFAULT_TIMEOUT) -> void:
	var timer := get_tree().create_timer(timeout)
	if await Async.timeout(target_signal, timer):
		fail("Timed out waiting for signal '%s' after %.1f seconds." % [target_signal.get_name(), timeout])


## Awaits a condition to become true within a timeout.
func wait_until(condition: Callable, timeout: float = DEFAULT_TIMEOUT) -> void:
	var timeout_timer := get_tree().create_timer(timeout)
	while not condition.call():
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			fail("Timed out waiting for condition after %.1f seconds." % timeout)
			return
