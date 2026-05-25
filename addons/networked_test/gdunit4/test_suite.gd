## GdUnit4 base class for tests that use the Networked addon.
##
## Provides timeout-safe await helpers, a [NetwTestHarness] factory, log
## controls, and a small entity builder for addon-internal unit tests.
class_name NetwTestSuite
extends GdUnitTestSuite

const DEFAULT_TIMEOUT := 1.0

const _GdUnitAwaiter := preload(
	"res://addons/networked_test/gdunit4/gdunit_awaiter.gd"
)


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


## Awaits a condition to become true within [param timeout] seconds.
func wait_until(condition: Callable, timeout: float = DEFAULT_TIMEOUT) -> void:
	var timeout_timer := get_tree().create_timer(timeout)
	while not condition.call():
		await get_tree().process_frame
		if timeout_timer.time_left <= 0:
			fail(
				"Timed out waiting for condition after %.1f seconds." % timeout)
			return


## Factory that creates a default [MultiplayerSceneManager] for tests.
## [br][br]
## Returns a fresh instance with no pre-configured exported properties.
## [codeblock]
## var mgr := NetwTestSuite.create_scene_manager()
## await harness.setup(mgr)
## [/codeblock]
static func create_scene_manager() -> MultiplayerSceneManager:
	var mgr := MultiplayerSceneManager.new()
	mgr.name = &"SceneManager"
	return mgr


## Drains the [SceneTree] of pending [code]queue_free[/code] calls and
## [code]call_deferred[/code] operations by awaiting [param count] frames.
## [br][br]
## Use in [method after_test] or when cleaning up complex scenes to prevent
## state leakage between test cases.
static func drain_frames(tree: SceneTree, count: int = 3) -> void:
	for i in count:
		await tree.process_frame


## Builds, parents, and auto-frees a [NetwTestHarness].
##
## The returned harness has the GdUnit4 awaiter installed. Always call
## [code]await harness.setup(...)[/code] before driving multiplayer flows.
##
## [codeblock]
## var harness := make_harness()
## await harness.setup(NetwTestSuite.create_scene_manager())
## var client := await harness.add_client()
## [/codeblock]
func make_harness() -> NetwTestHarness:
	var harness := NetwTestHarness.new()
	harness.awaiter = _GdUnitAwaiter.get_awaiter()
	add_child(harness)
	auto_free(harness)
	return harness


## Builds a [NetwEntity]-rooted node suitable for isolated unit and
## integration tests. Pre-attaches the entity via
## [constant NetwEntity.META_KEY] and sets [member Node.owner] so
## [method NetwEntity.of] short-circuits instead of walking past
## test-fixture ancestors. The returned root is registered with
## [code]auto_free[/code].
##
## [param parent] container the root is added under.
## [param entity_name] [member Node.name] for the entity root.
## [param peer_id] assigned to [member NetwEntity.peer_id].
## [param with_sync] when [code]true[/code], attaches a
##         [MultiplayerSynchronizer] child named [code]"Sync"[/code] so
##         interest drivers iterating [method NetwEntity.synchronizers]
##         find at least one target.
func make_test_entity(
		parent: Node,
		entity_name: String = "Ent",
		peer_id: int = 0,
		with_sync: bool = true,
) -> Node:
	var root := Node.new()
	root.name = entity_name
	var entity := NetwEntity.new()
	entity.peer_id = peer_id
	root.set_meta(NetwEntity.META_KEY, entity)
	entity.owner = root
	parent.add_child(root)
	auto_free(root)
	if with_sync:
		var sync_node := MultiplayerSynchronizer.new()
		sync_node.name = "Sync"
		root.add_child(sync_node)
	return root


## Enables [NetwLog] output for the current test case.
func enable_logs(logl: String = "trace") -> void:
	NetwTestSessionHook.enable_current_test_logs(logl)


## Enables reporter-backed [NetTrace] output for the current test case.
func enable_debugger() -> void:
	NetwTestSessionHook.enable_current_test_debugger()


func after_test() -> void:
	clean_temp_dir()
