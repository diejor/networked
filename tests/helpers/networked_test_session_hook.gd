## GdUnit4 session hook that owns Networked debug state during tests.
##
## Logging and debugger scopes are closed after each test case so failures and
## early returns cannot leak state into the next test.
class_name NetworkedTestSessionHook
extends GdUnitTestSessionHook

func _init() -> void:
	super("NetworkedTestHook", "Auto-resets the NetworkedDebugger between tests.")

static var _active_hook: NetworkedTestSessionHook

var _baseline_child_count: int = 0
var _session_log_scope: NetwLogScope
var _test_log_scope: NetwLogScope
var _test_debug_scope: NetwDbgScope
var _test_log_overrides: Dictionary = {}


## Enables logging for the currently running test.
static func enable_current_test_logs(logl: String = "trace") -> void:
	if _active_hook:
		_active_hook._open_test_log_scope(logl)


## Enables reporter-backed traces for the currently running test.
static func enable_current_test_debugger() -> void:
	if _active_hook:
		_active_hook._open_test_debug_scope()


func startup(session: GdUnitTestSession) -> GdUnitResult:
	assert(
		Netw.is_test_env(),
		"NetworkedTestHook: GdUnit4 environment not detected! " + \
		"Check markers (Engine meta or cmdline args)."
	)
	_active_hook = self
	NetwLog.set_test_hook_controls_overrides(true)
	
	var log_level := "none"

	if OS.has_environment("NETW_TEST_LOG"):
		log_level = OS.get_environment("NETW_TEST_LOG")

	for arg in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		if arg.begins_with("--netw-log="):
			log_level = arg.split("=")[1]
		elif arg.begins_with("--netw-log-test="):
			_parse_test_log_override(arg.substr("--netw-log-test=".length()))

	_session_log_scope = NetwLog.scoped(log_level)
	OS.set_environment("NETW_TEST_LOG", log_level)
	session.test_event.connect(_on_test_event)

	return GdUnitResult.success()

func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	_close_test_debug_scope()
	_close_test_log_scope()
	_close_session_log_scope()
	NetwLog.set_test_hook_controls_overrides(false)
	if _active_hook == self:
		_active_hook = null
	return GdUnitResult.success()


func _on_test_event(event: GdUnitEvent) -> void:
	if event.type() == GdUnitEvent.TESTSUITE_BEFORE:
		_baseline_child_count = Engine.get_main_loop().root.get_child_count()
		
	elif event.type() == GdUnitEvent.TESTCASE_BEFORE:
		_close_test_debug_scope()
		_close_test_log_scope()
		_reset_debugger()
		if _test_log_overrides.has(event.test_name()):
			_open_test_log_scope(_test_log_overrides[event.test_name()])
		
	elif event.type() == GdUnitEvent.TESTCASE_AFTER:
		_assert_clean_state(event)
		_close_test_debug_scope()
		_close_test_log_scope()


func _reset_debugger() -> void:
	var reporter := Netw.dbg.get_reporter()
	if reporter and reporter.has_method(&"reset_state"):
		reporter.reset_state()
	else:
		Netw.dbg.reset()
	
	if LocalLoopbackSession.shared:
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null

	FileSystemBackend._clear_path_registry()


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
		push_error(
			"TEST ISOLATION LEAK [%s]: LocalLoopbackSession.shared was " + \
			"not cleared." % [event.test_name()]
		)
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null


func _parse_test_log_override(raw: String) -> void:
	var parts := raw.split("=", false, 1)
	if parts.size() != 2:
		push_warning("NetworkedTestHook: ignored malformed --netw-log-test.")
		return
	_test_log_overrides[parts[0].strip_edges()] = parts[1].strip_edges()


func _open_test_log_scope(logl: String) -> void:
	_close_test_log_scope()
	_test_log_scope = NetwLog.scoped(logl)


func _close_test_log_scope() -> void:
	if _test_log_scope:
		_test_log_scope.close()
		_test_log_scope = null


func _open_test_debug_scope() -> void:
	_close_test_debug_scope()
	_test_debug_scope = Netw.dbg.enable_for_test()


func _close_test_debug_scope() -> void:
	if _test_debug_scope:
		_test_debug_scope.close()
		_test_debug_scope = null


func _close_session_log_scope() -> void:
	if _session_log_scope:
		_session_log_scope.close()
		_session_log_scope = null
