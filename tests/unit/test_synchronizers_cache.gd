## Unit tests for [SynchronizersCache], focused on the [method SynchronizersCache.register_provider]
## extension hook added alongside [ProxySynchronizer].
##
## [b]Static-state isolation:[/b] [member SynchronizersCache._providers] is a static variable
## that persists across test cases.  [method before_test] snapshots the array and
## [method after_test] restores it, so provider registrations made inside one test
## cannot bleed into another.
##
## [b]Coverage[/b]
## [ul]
## [li]Provider results are appended to the base MS-discovery result[/li]
## [li]Multiple providers are all called[/li]
## [li]Providers returning duplicates of already-found synchronizers are de-duplicated[/li]
## [li]An empty provider does not break the result[/li]
## [li]Providers are not called in editor mode (cache path is always live in tests, but the
##     provider mechanism itself is exercised via direct [method get_synchronizers] calls)[/li]
## [/ul]
class_name TestSynchronizersCache
extends NetworkedTestSuite

## Snapshot of [member SynchronizersCache._providers] saved before each test.
var _saved_providers: Array[Callable]


func before_test() -> void:
	_saved_providers = SynchronizersCache._providers.duplicate()
	SynchronizersCache._providers.clear()


func after_test() -> void:
	SynchronizersCache._providers = _saved_providers


# ---------------------------------------------------------------------------
# Helpers — build a minimal disconnected node tree with a synchronizer
# ---------------------------------------------------------------------------

## Creates a root [Node2D] with one [MultiplayerSynchronizer] whose
## [member MultiplayerSynchronizer.root_path] points back to root.
## All nodes share the same owner so [method SynchronizersCache.get_synchronizers]
## discovery works on disconnected trees.
func _make_node_with_sync() -> Array:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Root"

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	root.add_child(sync)
	sync.owner = root

	return [root, sync]


# ---------------------------------------------------------------------------
# register_provider — basic inclusion
# ---------------------------------------------------------------------------

func test_provider_result_included_in_get_synchronizers() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]

	var extra := MultiplayerSynchronizer.new()
	extra.name = "ExtraSync"
	extra.root_path = NodePath("..")
	root.add_child(extra)
	extra.owner = root

	# The base discovery only finds syncs whose root_path resolves to root.
	# We register a provider that explicitly returns `extra` for any node.
	SynchronizersCache.register_provider(func(_node: Node) -> Array:
		return [extra]
	)

	# Clear any metadata cache so discovery runs fresh.
	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	assert_that(result.has(extra)).is_true()


func test_provider_result_added_after_base_discovery() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]
	var base_sync: MultiplayerSynchronizer = parts[1]

	var injected := MultiplayerSynchronizer.new()
	injected.name = "Injected"
	root.add_child(injected)
	injected.owner = root

	SynchronizersCache.register_provider(func(_node: Node) -> Array:
		return [injected]
	)

	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	assert_that(result.has(base_sync)).is_true()
	assert_that(result.has(injected)).is_true()


# ---------------------------------------------------------------------------
# register_provider — multiple providers
# ---------------------------------------------------------------------------

func test_multiple_providers_all_called() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]

	var extra_a := MultiplayerSynchronizer.new()
	extra_a.name = "ExtraA"
	root.add_child(extra_a)
	extra_a.owner = root

	var extra_b := MultiplayerSynchronizer.new()
	extra_b.name = "ExtraB"
	root.add_child(extra_b)
	extra_b.owner = root

	SynchronizersCache.register_provider(func(_n: Node) -> Array: return [extra_a])
	SynchronizersCache.register_provider(func(_n: Node) -> Array: return [extra_b])

	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	assert_that(result.has(extra_a)).is_true()
	assert_that(result.has(extra_b)).is_true()


# ---------------------------------------------------------------------------
# register_provider — de-duplication
# ---------------------------------------------------------------------------

func test_provider_duplicate_not_added_twice() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]
	var base_sync: MultiplayerSynchronizer = parts[1]

	# Provider returns a sync that base discovery already found.
	SynchronizersCache.register_provider(func(_n: Node) -> Array:
		return [base_sync]
	)

	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	var count := 0
	for s in result:
		if s == base_sync:
			count += 1
	assert_that(count).is_equal(1)


# ---------------------------------------------------------------------------
# register_provider — empty provider is safe
# ---------------------------------------------------------------------------

func test_empty_provider_does_not_break_result() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]
	var base_sync: MultiplayerSynchronizer = parts[1]

	SynchronizersCache.register_provider(func(_n: Node) -> Array: return [])

	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	assert_that(result.has(base_sync)).is_true()


# ---------------------------------------------------------------------------
# register_provider — no providers registered (baseline)
# ---------------------------------------------------------------------------

func test_no_providers_returns_base_discovery_result() -> void:
	var parts := _make_node_with_sync()
	var root: Node2D = parts[0]
	var base_sync: MultiplayerSynchronizer = parts[1]

	# _providers is already cleared in before_test.
	SynchronizersCache.clear_cache(root)
	var result := SynchronizersCache.get_synchronizers(root)

	assert_that(result.has(base_sync)).is_true()
	assert_that(result.size()).is_equal(1)
