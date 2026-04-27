## GdUnit4 session hook that manages the [NetworkedDebugger] lifecycle during tests.
##
## Automatically resets the debugger state before every test case to prevent state 
## leaking, and maintains [NetwLog] settings.
class_name NetworkedTestSessionHook
extends GdUnitTestSessionHook

func _init() -> void:
	super("NetworkedTestHook", "Auto-resets the NetworkedDebugger between tests.")

var _baseline_child_count: int = 0

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
	if event.type() == GdUnitEvent.TESTSUITE_BEFORE:
		_baseline_child_count = Engine.get_main_loop().root.get_child_count()
		
	elif event.type() == GdUnitEvent.TESTCASE_BEFORE:
		# Ensure we start from a clean state
		_reset_debugger()
		
	elif event.type() == GdUnitEvent.TESTCASE_AFTER:
		# Check for leaks
		_assert_clean_state(event)


func _reset_debugger() -> void:
	if Engine.has_singleton("NetworkedDebugger"):
		Engine.get_singleton("NetworkedDebugger").reset_state()
	elif is_instance_valid(NetworkedDebugger):
		NetworkedDebugger.reset_state()
	
	if LocalLoopbackSession.shared:
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null


func _assert_clean_state(event: GdUnitEvent) -> void:
	var tree := Engine.get_main_loop() as SceneTree
	if not tree:
		return
		
	var root := tree.root
	var current_count: int = root.get_child_count()
	
	if current_count > _baseline_child_count:
		var leaks: Array[String] = []
		for i in range(_baseline_child_count, current_count):
			leaks.append(root.get_child(i).name)
		
		# We don't want to fail the test here as it might be hard to debug 
		# from a hook, but we should at least log an error.
		# Actually, pushing an error might be better.
		push_error("TEST ISOLATION LEAK [%s]: Leaked %d root children: %s" % [
			event.test_name(),
			current_count - _baseline_child_count,
			", ".join(leaks)
		])

	if not NetTrace._active.is_empty():
		push_error("TEST ISOLATION LEAK [%s]: Leaked %d NetTrace spans." % [
			event.test_name(),
			NetTrace._active.size()
		])
		NetTrace.reset()

	if LocalLoopbackSession.shared != null:
		push_error("TEST ISOLATION LEAK [%s]: LocalLoopbackSession.shared was not cleared." % [
			event.test_name()
		])
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null
