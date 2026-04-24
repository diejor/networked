## GdUnit4 session hook that manages the [NetworkedDebugger] lifecycle during tests.
##
## Automatically resets the debugger state before every test case to prevent state 
## leaking, and maintains [NetwLog] settings.
class_name NetworkedTestSessionHook
extends GdUnitTestSessionHook

func _init() -> void:
	super("NetworkedTestHook", "Auto-resets the NetworkedDebugger between tests.")

func startup(session: GdUnitTestSession) -> GdUnitResult:
	var log_level := "none"
	
	if OS.has_environment("NETW_TEST_LOG"):
		log_level = OS.get_environment("NETW_TEST_LOG")
	
	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--netw-log="):
			log_level = arg.split("=")[1]
			
	NetwLog.push_setting_str(log_level)
	session.test_event.connect(_on_test_event)
	return GdUnitResult.success()

func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	NetwLog.pop_settings()
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
