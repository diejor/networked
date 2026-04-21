## GdUnit4 session hook that manages the [NetworkedDebugger] lifecycle during tests.
##
## Automatically resets the debugger state before every test case to prevent state 
## leaking, and maintains [NetLog] settings.
class_name NetworkedTestSessionHook
extends GdUnitTestSessionHook

func _init() -> void:
	super("NetworkedTestHook", "Auto-resets the NetworkedDebugger between tests.")

func startup(session: GdUnitTestSession) -> GdUnitResult:
	NetLog.push_setting_str("none")
	session.test_event.connect(_on_test_event)
	return GdUnitResult.success()

func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	NetLog.pop_settings()
	return GdUnitResult.success()

func _on_test_event(event: GdUnitEvent) -> void:
	if event.type() == GdUnitEvent.TESTCASE_BEFORE:
		# If the debugger is an Autoload, it might not be in the tree if it's 
		# disabled, but it's always accessible by its class name if we have one, 
		# or via the Autoload name.
		if Engine.has_singleton("NetworkedDebugger"):
			Engine.get_singleton("NetworkedDebugger").reset_state()
		elif is_instance_valid(NetworkedDebugger):
			NetworkedDebugger.reset_state()
