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

var _netw_managed_harness: NetwTestHarness
var _netw_managed_game_harness: NetwGameHarness


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


## Builds, parents, and auto-tears down a [NetwTestHarness].
##
## The returned harness has the GdUnit4 awaiter installed. Always call
## [code]await harness.setup(...)[/code] before driving multiplayer flows.
## A test case may create one managed harness; [method after_test] tears it
## down automatically.
##
## [codeblock]
## var harness := make_harness()
## await harness.setup(NetwTestSuite.create_scene_manager())
## var client := await harness.add_client()
## [/codeblock]
func make_harness() -> NetwTestHarness:
	assert(
		_netw_managed_harness == null,
		"make_harness: harness already created.",
	)
	_netw_managed_harness = make_unmanaged_harness()
	return _netw_managed_harness


## Builds, parents, and auto-frees an unmanaged [NetwTestHarness].
##
## Use this only for additional harnesses inside a test case. The caller must
## explicitly call [code]await harness.teardown()[/code].
func make_unmanaged_harness() -> NetwTestHarness:
	var harness := NetwTestHarness.new()
	harness.awaiter = _GdUnitAwaiter.get_awaiter()
	add_child(harness)
	auto_free(harness)
	return harness


## Builds, parents, and auto-tears down a [NetwGameHarness].
##
## The returned harness has the GdUnit4 reporter installed. Always call
## [code]await game.setup()[/code] before adding peers.
func make_game_harness(scene: PackedScene) -> NetwGameHarness:
	assert(
		_netw_managed_game_harness == null,
		"make_game_harness: harness already created.",
	)
	_netw_managed_game_harness = make_unmanaged_game_harness(scene)
	return _netw_managed_game_harness


## Builds, parents, and auto-frees an unmanaged [NetwGameHarness].
##
## Use this only for additional game harnesses inside a test case. The caller
## must explicitly call [code]await harness.teardown()[/code].
func make_unmanaged_game_harness(scene: PackedScene) -> NetwGameHarness:
	var harness := NetwGameHarness.new(scene)
	harness.reporter = _GdUnitAwaiter.get_reporter()
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
	if is_instance_valid(_netw_managed_harness):
		await _netw_managed_harness.teardown()
	_netw_managed_harness = null
	if is_instance_valid(_netw_managed_game_harness):
		await _netw_managed_game_harness.teardown()
	_netw_managed_game_harness = null
	clean_temp_dir()
