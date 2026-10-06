## GdUnit4-adapted eraser for errors a test knowingly tolerates.
##
## A native library may log an error that is harmless and unavoidable, and
## GdUnit4 turns any recorded error into a failure. Erasing one is reaching into
## the framework's own error monitor, which is why it lives here rather than in
## the harness: everything under [code]harness/[/code] runs under any framework
## or none, and this runs under exactly one.
##
## [method NetwTestSessionHook.startup] installs it, the way
## [method NetwTestSuite.make_game_harness] installs the timeout reporter. A caller
## outside GdUnit4 leaves it unassigned and erases nothing, which is correct: a
## framework that records no errors has none to erase.
extends RefCounted


## Returns an eraser as a [Callable] matching
## [code]func(Array[String]) -> int[/code].
static func get_eraser() -> Callable:
	return erase_matching


## Erases every recorded error or warning whose message contains all of
## [param markers], and returns how many it erased.
##
## Every marker must match, so a caller narrows by adding one rather than by
## writing a pattern. A case that provokes a fault on purpose asserts the count,
## so the fault is checked rather than hidden.
## [codeblock]
## assert_int(await erase_matching(["joined with no arguments"])).is_equal(1)
## [/codeblock]
static func erase_matching(markers: Array) -> int:
	var monitor := (
		GdUnitThreadManager.get_current_context().get_execution_context().error_monitor
	)
	var entries: Array[ErrorLogEntry] = await monitor.scan(true)
	var erased := 0
	for entry: ErrorLogEntry in entries.duplicate():
		var message: String = entry._message
		var matched := true
		for marker: String in markers:
			if marker not in message:
				matched = false
				break
		if matched:
			monitor.erase_log_entry(entry)
			erased += 1
	return erased
