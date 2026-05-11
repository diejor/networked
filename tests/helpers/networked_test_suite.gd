## Base class for networked tests that provides timeout-safe await helpers.
class_name NetworkedTestSuite
extends GdUnitTestSuite

const DEFAULT_TIMEOUT := 1.0


## Awaits a signal and fails the test if it times out.
## [codeblock]
## await timeout_await(my_signal, 1.0)
## [/codeblock]
func timeout_await(
	target_signal: Signal,
	timeout: float = DEFAULT_TIMEOUT
) -> void:
	var timer := get_tree().create_timer(timeout)
	if await Async.timeout(target_signal, timer):
		fail(
			"Timed out waiting for signal '%s' after %.1f seconds." % [
				target_signal.get_name(),
				timeout,
			]
		)


## Awaits a condition to become true within a timeout.
func wait_until(condition: Callable, timeout: float = DEFAULT_TIMEOUT) -> void:
	var timeout_timer := get_tree().create_timer(timeout)
	while not condition.call():
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			fail("Timed out waiting for condition after %.1f seconds." % timeout)
			return


## Factory that creates a default [MultiplayerSceneManager] for tests.
## [br][br]
## Returns a fresh instance with no pre-configured exported properties,
## equivalent to instantiating the deleted scene manager [code].tscn[/code]
## that was previously referenced by UID.
## [codeblock]
## var mgr := NetworkedTestSuite.create_scene_manager()
## harness.setup(mgr)
## [/codeblock]
static func create_scene_manager() -> MultiplayerSceneManager:
	var mgr := MultiplayerSceneManager.new()
	mgr.name = &"SceneManager"
	return mgr


## Drains the [SceneTree] of pending [code]queue_free[/code] calls and 
## [code]call_deferred[/code] operations by awaiting multiple frames.
## [br][br]
## Use this in [method after_test] or when cleaning up complex scenes to 
## prevent state leakage between test cases.
static func drain_frames(tree: SceneTree, count: int = 3) -> void:
	for i in count:
		await tree.process_frame


## Enables [NetwLog] output for the current test case.
func enable_logs(logl: String = "trace") -> void:
	NetworkedTestSessionHook.enable_current_test_logs(logl)


## Enables reporter-backed [NetTrace] output for the current test case.
func enable_debugger() -> void:
	NetworkedTestSessionHook.enable_current_test_debugger()


func after_test() -> void:
	clean_temp_dir()
