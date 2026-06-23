## GdUnit4 session hook that owns Networked debug state during tests.
##
## Logging and debugger scopes are closed after each test case so failures and
## early returns cannot leak state into the next test. Also baselines the
## SceneTree root child count and the engine's
## [code]OBJECT_RESOURCE_COUNT[/code] performance monitor to surface isolation
## leaks.
class_name NetwTestSessionHook
extends GdUnitTestSessionHook

static var _active_hook: NetwTestSessionHook
static var game_harness_used_in_test: bool = false

var _baseline_child_count: int = 0
var _baseline_resource_count: int = 0
var _baseline_time_scale: float = 1.0
var _baseline_physics_ticks: int = 60
var _pre_test_resource_count: int = 0
var _top_resource_growths: Array = []
const _TOP_RESOURCE_GROWTH_LIMIT := 5
var _session: GdUnitTestSession
var _session_log_scope: NetwLogScope
var _test_log_scope: NetwLogScope
var _test_debug_scope: NetwDbgScope
var _test_log_overrides: Dictionary = { }


func _init() -> void:
	super("NetwTestHook", "Auto-resets the Networked debugger between tests.")


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
		"NetwTestHook: GdUnit4 environment not detected! " +
		"Check markers (Engine meta or cmdline args).",
	)
	_active_hook = self
	_session = session
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
	_baseline_resource_count = int(
		Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT),
	)
	# Snapshot the clean engine timing config before any harness runs, so the
	# per-test reset can undo the headless 10x speedup NetwGameHarness installs
	# even when a harness teardown is skipped.
	_baseline_time_scale = Engine.time_scale
	_baseline_physics_ticks = Engine.get_physics_ticks_per_second()

	return GdUnitResult.success()


func shutdown(_session: GdUnitTestSession) -> GdUnitResult:
	_report_resource_delta()
	_reset_global_test_state()
	_close_test_debug_scope()
	_close_test_log_scope()
	_close_session_log_scope()
	NetwLog.set_test_hook_controls_overrides(false)
	if _active_hook == self:
		_active_hook = null
	_session = null
	return GdUnitResult.success()


func _on_test_event(event: GdUnitEvent) -> void:
	if event.type() == GdUnitEvent.TESTSUITE_BEFORE:
		# Ensure InputMap is fully loaded from project settings.
		# This is essential on fresh CI environments without prior editor import.
		InputMap.load_from_project_settings()
		_baseline_child_count = Engine.get_main_loop().root.get_child_count()

	elif event.type() == GdUnitEvent.TESTCASE_BEFORE:
		game_harness_used_in_test = false
		_close_test_debug_scope()
		_close_test_log_scope()
		_reset_debugger()
		NetwLog.start_test_case_buffering()
		_pre_test_resource_count = int(
			Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT),
		)
		if _test_log_overrides.has(event.test_name()):
			_open_test_log_scope(_test_log_overrides[event.test_name()])

	elif event.type() == GdUnitEvent.TESTCASE_AFTER:
		_assert_clean_state(event)
		_reset_global_test_state()
		_track_resource_delta(event)
		_close_test_debug_scope()
		_close_test_log_scope()
		NetwLog.stop_test_case_buffering()


func _reset_debugger() -> void:
	var reporter := Netw.dbg.get_reporter()
	if reporter and reporter.has_method(&"reset_state"):
		reporter.reset_state()
	else:
		Netw.dbg.reset()

	_reset_global_test_state()


func _reset_global_test_state() -> void:
	NetwPathNamespace.reset()

	# NetwGameHarness scales engine timing 10x under headless and restores it only
	# in its own teardown. Force the clean baseline back between tests so a skipped
	# teardown cannot leak a 10x physics rate into a later suite, where it would
	# desync Engine.get_physics_ticks_per_second() from the static project setting
	# that LocalLoopbackSession derives its delay clock from.
	if Engine.time_scale != _baseline_time_scale:
		Engine.time_scale = _baseline_time_scale
	if Engine.get_physics_ticks_per_second() != _baseline_physics_ticks:
		Engine.set_physics_ticks_per_second(_baseline_physics_ticks)

	if LocalLoopbackSession.shared:
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null

	FileSystemDatabase._clear_path_registry()
	WebTorrentTrackerClient.clear_shared_clients()


func _assert_clean_state(event: GdUnitEvent) -> void:
	var tree := Engine.get_main_loop() as SceneTree
	if not tree:
		return

	var root := tree.root
	var current_count: int = root.get_child_count()

	if current_count > _baseline_child_count:
		var leaks: Array[String] = []
		for i in range(_baseline_child_count, current_count):
			var child := root.get_child(i)
			leaks.append("%s:<%s>" % [child.name, child.get_class()])

		push_error(
			"TEST ISOLATION LEAK [%s]: Leaked %d root children: %s" % [
				event.test_name(),
				current_count - _baseline_child_count,
				", ".join(leaks),
			],
		)

	if not NetTrace._active.is_empty():
		push_error(
			"TEST ISOLATION LEAK [%s]: Leaked %d NetTrace spans." % [
				event.test_name(),
				NetTrace._active.size(),
			],
		)
		NetTrace.reset()

	if LocalLoopbackSession.shared != null:
		push_error(
			"TEST ISOLATION LEAK [%s]: LocalLoopbackSession.shared was " +
			"not cleared." % [event.test_name()],
		)
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null


func _track_resource_delta(event: GdUnitEvent) -> void:
	if game_harness_used_in_test:
		return
	var current_count := int(
		Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT),
	)
	var growth := current_count - _pre_test_resource_count
	if growth <= 0:
		return
	_top_resource_growths.append(
		{
			"label": _resolve_test_label(event),
			"growth": growth,
			"after": current_count,
		},
	)
	_top_resource_growths.sort_custom(
		func(a, b): return a["growth"] > b["growth"]
	)
	if _top_resource_growths.size() > _TOP_RESOURCE_GROWTH_LIMIT:
		_top_resource_growths.resize(_TOP_RESOURCE_GROWTH_LIMIT)


func _resolve_test_label(event: GdUnitEvent) -> String:
	if _session:
		var tc := _session.find_test_by_id(event.guid())
		if tc:
			return "%s::%s" % [tc.suite_name, tc.test_name]
	return "<unknown>"


func _report_resource_delta() -> void:
	if _top_resource_growths.is_empty():
		return
	var lines: Array[String] = []
	for entry in _top_resource_growths:
		lines.append(
			"  +%d (now %d) %s" % [
				entry["growth"],
				entry["after"],
				entry["label"],
			],
		)
	push_warning(
		"TEST RESOURCE GROWTH (top %d offenders, baseline %d):\n%s" % [
			_top_resource_growths.size(),
			_baseline_resource_count,
			"\n".join(lines),
		],
	)


func _parse_test_log_override(raw: String) -> void:
	var parts := raw.split("=", false, 1)
	if parts.size() != 2:
		push_warning("NetwTestHook: ignored malformed --netw-log-test.")
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
