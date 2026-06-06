## GdUnit4-adapted awaiter for [NetwTestHarness]. Pure standalone function
## (no [GdUnitTestSuite] reference required) that times out a signal and
## reports failures via [code]GdAssertReports.report_error[/code] so they
## land in the GdUnit4 test report.
##
## Returns [code]true[/code] on timeout, [code]false[/code] on success,
## matching the awaiter contract
## [code]func(Signal, float, String) -> bool[/code].
##
## Wired automatically by [method NetwTestSuite.make_harness]. Outside
## GdUnit4 (plain Godot), [member NetwTestHarness.awaiter] defaults to a
## [code]push_error[/code]-backed implementation instead.
extends RefCounted

## Returns the awaiter as a [Callable] matching the contract.
static func get_awaiter() -> Callable:
	return await_signal


## Returns a timeout reporter as a [Callable] matching
## [code]func(String, float) -> void[/code].
static func get_reporter() -> Callable:
	return report_timeout


## Awaits [param sig] with [param timeout] seconds. On timeout, reports
## via [code]GdAssertReports.report_error[/code] and returns
## [code]true[/code]. [param label] is included in the failure message.
static func await_signal(sig: Signal, timeout: float, label: String) -> bool:
	var tree := Engine.get_main_loop() as SceneTree
	var timer := tree.create_timer(timeout)
	var timed_out: bool = await Async.timeout(sig, timer)
	if timed_out:
		var name := label if not label.is_empty() else String(sig.get_name())
		GdAssertReports.report_error(
			"Timed out waiting for '%s' after %.2fs." % [name, timeout],
			-1,
		)
	return timed_out


## Reports a timeout for [param label] after [param timeout] seconds.
static func report_timeout(label: String, timeout: float) -> void:
	GdAssertReports.report_error(
		"Timed out waiting for '%s' after %.2fs." % [label, timeout],
		-1,
	)
