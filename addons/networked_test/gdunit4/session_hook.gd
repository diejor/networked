## GdUnit4 session hook that owns Networked debug state during tests.
##
## Debugger scopes are closed after each test case so failures and early
## returns cannot leak state into the next test. Also baselines the SceneTree
## root child count and the engine's [code]OBJECT_RESOURCE_COUNT[/code]
## performance monitor to surface isolation leaks.
class_name NetwTestSessionHook
extends GdUnitTestSessionHook

static var _active_hook: NetwTestSessionHook
static var game_harness_used_in_test: bool = false

const _GdUnitErrorScrubber := preload(
		"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)

var _baseline_child_count: int = 0
var _baseline_resource_count: int = 0
var _baseline_time_scale: float = 1.0
var _baseline_physics_ticks: int = 60
var _pre_test_resource_count: int = 0
var _top_resource_growths: Array = []
const _TOP_RESOURCE_GROWTH_LIMIT := 5
var _session: GdUnitTestSession


func _init() -> void:
	super("NetwTestHook", "Resets shared Networked state between tests.")


func startup(session: GdUnitTestSession) -> GdUnitResult:
	assert(
		Engine.has_meta("GdUnitRunner"),
		"NetwTestHook: GdUnit4 environment not detected! " +
		"Check markers (Engine meta or cmdline args).",
	)
	_active_hook = self
	_session = session
	# The harness half runs under any framework, so the one piece that reaches
	# into this framework's error monitor is handed to it from here.
	WebRTCTestSupport.erase_benign_error = _GdUnitErrorScrubber.get_eraser()
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
		_reset_global_test_state()
		_pre_test_resource_count = int(
			Performance.get_monitor(Performance.OBJECT_RESOURCE_COUNT),
		)

	elif event.type() == GdUnitEvent.TESTCASE_AFTER:
		_assert_clean_state(event)
		_reset_global_test_state()
		_track_resource_delta(event)


func _reset_global_test_state() -> void:
	# Schema declarations are process-wide static data that outlives the session
	# that adopted them, so a suite declaring one would otherwise hand every
	# later suite a table it never asked for. A table's wire id is its
	# name-sorted position among the bound tables, so one leaked declaration
	# renumbers another suite's frames and the failure names the wrong cause.
	NetwNativeTests.schema_model_clear()

	# NetwGameHarness scales engine timing 10x under headless and restores it only
	# in its own teardown. Force the clean baseline back between tests so a skipped
	# teardown cannot leak a 10x physics rate into a later suite, where it would
	# desync Engine.get_physics_ticks_per_second() from the static project setting
	# that LocalLoopbackSession derives its delay clock from.
	if Engine.time_scale != _baseline_time_scale:
		Engine.time_scale = _baseline_time_scale
	if Engine.get_physics_ticks_per_second() != _baseline_physics_ticks:
		Engine.set_physics_ticks_per_second(_baseline_physics_ticks)

	# Sole owner of shared-session cleanup: this runs before and after every
	# test, so each case starts from a null shared regardless of how the prior
	# one left it.
	if LocalLoopbackSession.has_shared_session():
		LocalLoopbackSession.get_shared_session().reset()
		LocalLoopbackSession.set_shared_session(null)

	NetwNativeTests.file_system_database_forget_roots()
	NetwNativeTests.tracker_book_clear()


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

	# The shared loopback session is not asserted here. Unlike orphaned root
	# children, a lingering shared pointer never crosses into the
	# next test because _reset_global_test_state clears it before every case.
	# A harness releases it symmetrically in its own teardown.


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
