## GdUnit4-adapted timeout reporter for Networked test harnesses.
##
## [method NetwTestSuite.make_harness] and
## [method NetwTestSuite.make_game_harness] install this reporter so timeouts
## land in the GdUnit4 test report. Plain Godot callers can use the harness
## default reporter or assign their own.
extends RefCounted

## Returns a timeout reporter as a [Callable] matching
## [code]func(String, float) -> void[/code].
static func get_reporter() -> Callable:
	return report_timeout


## Reports a timeout for [param label] after [param timeout] seconds.
static func report_timeout(label: String, timeout: float) -> void:
	GdAssertReports.report_error(
		"Timed out waiting for '%s' after %.2fs." % [label, timeout],
		-1,
	)
