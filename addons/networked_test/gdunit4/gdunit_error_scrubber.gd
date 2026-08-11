## GdUnit4-adapted eraser for errors a test knowingly tolerates.
##
## A native library may log an error that is harmless and unavoidable, and
## GdUnit4 turns any recorded error into a failure. Erasing one is reaching into
## the framework's own error monitor, which is why it lives here rather than in
## the harness: everything under [code]harness/[/code] runs under any framework
## or none, and this runs under exactly one.
##
## [method NetwTestSessionHook.startup] installs it, the way
## [method NetwTestSuite.make_harness] installs the timeout reporter. A caller
## outside GdUnit4 leaves it unassigned and erases nothing, which is correct: a
## framework that records no errors has none to erase.
extends RefCounted


## Returns an eraser as a [Callable] matching
## [code]func(Array[String]) -> void[/code].
static func get_eraser() -> Callable:
	return erase_matching


## Erases every recorded error whose message contains all of [param markers].
##
## Every marker must match, so a caller narrows by adding one rather than by
## writing a pattern. Erasing is deliberately all-or-nothing per entry: an error
## that matched is one the caller has already argued is benign.
static func erase_matching(markers: Array) -> void:
	var monitor := (
		GdUnitThreadManager.get_current_context().get_execution_context().error_monitor
	)
	var entries: Array[ErrorLogEntry] = await monitor.scan(true)
	for entry: ErrorLogEntry in entries.duplicate():
		var message: String = entry._message
		var matched := true
		for marker: String in markers:
			if marker not in message:
				matched = false
				break
		if matched:
			monitor.erase_log_entry(entry)
