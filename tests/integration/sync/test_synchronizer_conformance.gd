## Conformance gate for [MultiplayerSynchronizer]: an unmodified stock project,
## a plain synchronizer carrying sync, watch, and spawn properties under a
## [MultiplayerTree] with no Networked nodes, must behave exactly as the native
## [code]SceneReplicationInterface[/code] contract promises.
##
## Each test pins one guarantee a stock project relies on: sync properties reach
## visible peers every tick, watch properties replicate reliably on change,
## spawn properties apply before the node enters the tree, the
## [signal MultiplayerSynchronizer.synchronized] signal fires on receivers at
## apply, and a late joiner heals to the current state.
class_name TestSynchronizerConformance
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var probe_scene: PackedScene
var _multi_sync_scene: PackedScene
var _sub_node_scene: PackedScene


func before_test() -> void:
	probe_scene = _make_probe_scene()
	_multi_sync_scene = _make_multi_sync_scene()
	_sub_node_scene = _make_sub_node_scene()
	StockSyncProbe.packed_scene_path = probe_scene.resource_path
	StockSyncProbe.extra_scene_paths = [
		_multi_sync_scene.resource_path,
		_sub_node_scene.resource_path,
	]
	harness = make_harness()
	await harness.setup(_make_world_scene())
	client0 = await harness.add_client()
	await drain_frames(get_tree(), 2)


func after_test() -> void:
	StockSyncProbe.packed_scene_path = ""
	StockSyncProbe.extra_scene_paths = []


# A spawn property is visible on the receiver before the node enters the tree,
# so _enter_tree already observes the authored value.
func test_spawn_property_applies_before_enter_tree() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.spawn_value = "spawned"
	node.name = "SpawnProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "SpawnProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.spawn_value).is_equal("spawned")
	assert_str(client_node.enter_tree_spawn_value).is_equal("spawned")


# A sync property authored on the server reaches a visible peer.
func test_sync_property_replicates_to_visible_peer() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "SyncProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "SyncProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()

	node.synced_value = 77
	var got: int = await _wait_value(func() -> int: return client_node.synced_value, 77)
	assert_int(got).is_equal(77)


# A watch property replicates its new value to a peer after it changes.
func test_watch_property_replicates_on_change() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "WatchProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "WatchProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()

	node.watched_value = 42
	var got: int = await _wait_value(func() -> int: return client_node.watched_value, 42)
	assert_int(got).is_equal(42)


# The synchronizer emits synchronized on the receiver when it applies an
# incoming sync, the signal user code and interpolation bind to.
func test_synchronized_signal_fires_on_receiver() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "SignalProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "SignalProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()

	var sync := _sync_of(client_node)
	assert_that(sync).is_not_null()
	var fired := [0]
	sync.synchronized.connect(func() -> void: fired[0] += 1)

	node.synced_value = 5
	await _wait_value(
		func() -> int: return fired[0],
		1,
		func(v: int) -> bool:
			return v >= 1
	)
	assert_int(fired[0]).is_greater(0)


# A peer that joins after the synced value already changed heals to the current
# value rather than the spawn-time default.
func test_late_joiner_receives_current_synced_state() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "LateProbe"
	_arena(harness.server()).add_child(node, true)

	var c0_node := await _wait_arena_child(client0, "LateProbe") \
			as StockSyncProbe
	assert_that(c0_node).is_not_null()
	node.synced_value = 123
	await _wait_value(func() -> int: return c0_node.synced_value, 123)

	var client1 := await harness.add_client()
	var c1_node := await _wait_arena_child(client1, "LateProbe") \
			as StockSyncProbe
	assert_that(c1_node).is_not_null()
	var got: int = await _wait_value(func() -> int: return c1_node.synced_value, 123)
	assert_int(got).is_equal(123)


# A peer hidden by the synchronizer's visibility never receives the node, and
# toggling it visible later heals the peer to the current synced value.
func test_visibility_api_gates_and_heals() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "HiddenProbe"
	var server_sync := node.get_node("Sync") as MultiplayerSynchronizer
	server_sync.public_visibility = false
	_arena(harness.server()).add_child(node, true)
	node.synced_value = 9

	# The hidden peer sees nothing while visibility denies it.
	var absent := await _wait_arena_child(client0, "HiddenProbe", 40)
	assert_that(absent).is_null()

	var client_peer := client0.multiplayer_peer.get_unique_id()
	server_sync.set_visibility_for(client_peer, true)
	server_sync.update_visibility()

	var client_node := await _wait_arena_child(client0, "HiddenProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()
	var got: int = await _wait_value(func() -> int: return client_node.synced_value, 9)
	assert_int(got).is_equal(9)


# Two synchronizers under one root each carry their own field and both replicate,
# addressed by distinct ordinals on the same route.
func test_multiple_synchronizers_per_root() -> void:
	var node := _multi_sync_scene.instantiate() as StockSyncProbe
	node.name = "MultiProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "MultiProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()

	node.synced_value = 11
	node.watched_value = 22
	var first: int = await _wait_value(func() -> int: return client_node.synced_value, 11)
	var second: int = await _wait_value(func() -> int: return client_node.watched_value, 22)
	assert_int(first).is_equal(11)
	assert_int(second).is_equal(22)


# A synchronizer whose config addresses a property on a sub-node resolves the
# sub-node path on both gather and apply, so the child property replicates.
func test_sub_node_property_replicates() -> void:
	var node := _sub_node_scene.instantiate() as StockSyncProbe
	node.name = "SubProbe"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "SubProbe") as StockSyncProbe
	assert_that(client_node).is_not_null()

	var visual := node.get_node("Visual") as Node2D
	visual.modulate = Color(0.25, 0.5, 0.75, 1.0)
	var client_visual := client_node.get_node("Visual") as Node2D
	var got: Color = await _wait_value(
		func() -> Color: return client_visual.modulate,
		visual.modulate,
	)
	assert_that(got).is_equal(Color(0.25, 0.5, 0.75, 1.0))


# Moving the synchronizer's authority to a client mid-session flips who may
# author the stream: the new authority's writes now reach the old one.
func test_authority_change_moves_the_stream() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "AuthProbe"
	_arena(harness.server()).add_child(node, true)
	node.synced_value = 3

	var client_node := await _wait_arena_child(client0, "AuthProbe") \
			as StockSyncProbe
	assert_that(client_node).is_not_null()
	await _wait_value(func() -> int: return client_node.synced_value, 3)

	# Both peers agree the client now owns the synchronizer. A bare synchronizer
	# has no entity arm() to apply authority, so this transfers it by hand.
	var client_peer := client0.multiplayer_peer.get_unique_id()
	_sync_of(node).set_multiplayer_authority(client_peer) # SMELL(authority-pin): no arm() on a bare synchronizer
	_sync_of(client_node).set_multiplayer_authority(client_peer) # SMELL(authority-pin): no arm() on a bare synchronizer

	client_node.synced_value = 44
	var got: int = await _wait_value(func() -> int: return node.synced_value, 44)
	assert_int(got).is_equal(44)


# A session torn down and re-hosted synchronizes again: the consumed bindings
# re-derive their routes against the fresh session rather than staying wedged.
func test_session_reset_and_re_host_resyncs() -> void:
	var node := probe_scene.instantiate() as StockSyncProbe
	node.name = "FirstHostProbe"
	_arena(harness.server()).add_child(node, true)
	node.synced_value = 5
	var first := await _wait_arena_child(client0, "FirstHostProbe") \
			as StockSyncProbe
	assert_that(first).is_not_null()
	await _wait_value(func() -> int: return first.synced_value, 5)

	await harness.teardown()
	harness = make_unmanaged_harness()
	await harness.setup(_make_world_scene())
	client0 = await harness.add_client()
	await drain_frames(get_tree(), 2)

	var node2 := probe_scene.instantiate() as StockSyncProbe
	node2.name = "SecondHostProbe"
	_arena(harness.server()).add_child(node2, true)
	node2.synced_value = 6
	var second := await _wait_arena_child(client0, "SecondHostProbe") \
			as StockSyncProbe
	assert_that(second).is_not_null()
	var got: int = await _wait_value(func() -> int: return second.synced_value, 6)
	assert_int(got).is_equal(6)


# A spawner-less synchronizer on a manual node pair still replicates: a synced
# root with no identity is adopted in place, stamping the same route onto the
# instance every peer already holds at the matching tree location.
func test_spawnerless_synchronizer_adopts_in_place() -> void:
	# The receiver must already hold the node when the adopt frame lands, so the
	# client instance is built before the server arms the adoption.
	var client_manual := _make_manual_probe()
	client0.add_child(client_manual)
	auto_free(client_manual)

	var server_manual := _make_manual_probe()
	harness.server().add_child(server_manual)
	auto_free(server_manual)
	await drain_frames(get_tree(), 4)

	server_manual.synced_value = 71
	var got: int = await _wait_value(
		func() -> int: return client_manual.synced_value,
		71,
	)
	assert_int(got).is_equal(71)


func _make_manual_probe() -> StockSyncProbe:
	var root := StockSyncProbe.new()
	root.name = "AdoptProbe"
	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var synced := NodePath(".:synced_value")
	cfg.add_property(synced)
	cfg.property_set_replication_mode(
		synced,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root
	return root


# The ownership capstone: across a full session of spawns, syncs, deltas, and a
# late join, every replication event is the pipeline's. The native replicator
# would announce any node it tracked through the spawner's spawned signal and
# would place a duplicate under the spawn path, so an exact spawn count and
# child count prove it received nothing and its loop ran empty.
func test_native_replicator_stays_empty_across_full_session() -> void:
	var client0_spawns := [0]
	_spawner(client0).spawned.connect(func(_n: Node) -> void: client0_spawns[0] += 1)

	var names := ["Cap0", "Cap1", "Cap2"]
	for probe_name in names:
		var node := probe_scene.instantiate() as StockSyncProbe
		node.name = probe_name
		_arena(harness.server()).add_child(node, true)
	for probe_name in names:
		assert_that(await _wait_arena_child(client0, probe_name)).is_not_null()

	# Each node spawned exactly once and nothing extra sits under the spawn path.
	assert_int(client0_spawns[0]).is_equal(3)
	assert_int(_arena(client0).get_child_count()).is_equal(3)

	# Sync and delta both apply on the receiver.
	var server0 := _arena(harness.server()).get_node("Cap0") as StockSyncProbe
	server0.synced_value = 100
	server0.watched_value = 200
	var c0 := _arena(client0).get_node("Cap0") as StockSyncProbe
	assert_int(await _wait_value(func() -> int: return c0.synced_value, 100)).is_equal(100)
	assert_int(await _wait_value(func() -> int: return c0.watched_value, 200)).is_equal(200)

	# A late joiner heals to exactly the three live nodes, again with no native
	# duplicate under the spawn path.
	var client1 := await harness.add_client()
	for probe_name in names:
		assert_that(await _wait_arena_child(client1, probe_name)).is_not_null()
	assert_int(_arena(client1).get_child_count()).is_equal(3)
	var c1 := _arena(client1).get_node("Cap0") as StockSyncProbe
	assert_int(await _wait_value(func() -> int: return c1.synced_value, 100)).is_equal(100)


func _spawner(mt: MultiplayerTree) -> MultiplayerSpawner:
	return mt.get_node("StockSyncWorld/StockSyncSpawner") as MultiplayerSpawner


func _arena(mt: MultiplayerTree) -> Node:
	return mt.get_node("StockSyncWorld/Arena")


func _sync_of(probe: Node) -> MultiplayerSynchronizer:
	return probe.get_node_or_null("Sync") as MultiplayerSynchronizer


func _wait_arena_child(
		mt: MultiplayerTree,
		child_name: String,
		frames: int = 120,
) -> Node:
	for i in frames:
		var found := _arena(mt).get_node_or_null(child_name)
		if found:
			return found
		await get_tree().process_frame
	return null


# Polls [param getter] until it satisfies [param predicate] (default equality
# with [param want]) or the frame budget runs out, then returns its last value.
func _wait_value(
		getter: Callable,
		want: Variant,
		predicate: Callable = Callable(),
		frames: int = 180,
) -> Variant:
	var check := predicate
	if not check.is_valid():
		check = func(v: Variant) -> bool: return v == want
	var last: Variant = getter.call()
	for i in frames:
		last = getter.call()
		if check.call(last):
			return last
		await get_tree().process_frame
	return last


func _make_probe_scene() -> PackedScene:
	var root := StockSyncProbe.new()
	root.name = "SyncStockProbe"
	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()

	var spawn_prop := NodePath(".:spawn_value")
	cfg.add_property(spawn_prop)
	cfg.property_set_spawn(spawn_prop, true)
	cfg.property_set_replication_mode(
		spawn_prop,
		SceneReplicationConfig.REPLICATION_MODE_NEVER,
	)

	var sync_prop := NodePath(".:synced_value")
	cfg.add_property(sync_prop)
	cfg.property_set_replication_mode(
		sync_prop,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)

	var watch_prop := NodePath(".:watched_value")
	cfg.add_property(watch_prop)
	cfg.property_set_replication_mode(
		watch_prop,
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
	)

	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root
	var path := NetwPathNamespace.next_path("player", "SyncStockProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


# A probe carrying two synchronizers, each with its own always-replicated field,
# so the suite can assert both replicate under distinct ordinals on one route.
func _make_multi_sync_scene() -> PackedScene:
	var root := StockSyncProbe.new()
	root.name = "MultiSyncProbe"

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var first := NodePath(".:synced_value")
	cfg.add_property(first)
	cfg.property_set_replication_mode(
		first,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root

	var sync2 := MultiplayerSynchronizer.new()
	sync2.name = "Sync2"
	sync2.root_path = NodePath("..")
	var cfg2 := SceneReplicationConfig.new()
	var second := NodePath(".:watched_value")
	cfg2.add_property(second)
	cfg2.property_set_replication_mode(
		second,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync2.replication_config = cfg2
	root.add_child(sync2)
	sync2.owner = root

	var path := NetwPathNamespace.next_path("player", "MultiSyncProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


# A probe whose synchronizer addresses a property on a child node, so the suite
# can assert sub-node path resolution at gather and apply.
func _make_sub_node_scene() -> PackedScene:
	var root := StockSyncProbe.new()
	root.name = "SubNodeProbe"
	var visual := Node2D.new()
	visual.name = "Visual"
	root.add_child(visual)
	visual.owner = root

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var modulate := NodePath("Visual:modulate")
	cfg.add_property(modulate)
	cfg.property_set_replication_mode(
		modulate,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root

	var path := NetwPathNamespace.next_path("player", "SubNodeProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _make_world_scene() -> PackedScene:
	var world := StockSyncWorld.new()
	world.name = "StockSyncWorld"
	var arena := Node.new()
	arena.name = "Arena"
	world.add_child(arena)
	arena.owner = world
	var spawner := MultiplayerSpawner.new()
	spawner.name = "StockSyncSpawner"
	spawner.spawn_path = NodePath("../Arena")
	world.add_child(spawner)
	spawner.owner = world
	var packed := PackedScene.new()
	var err := packed.pack(world)
	assert(err == OK, "failed to pack the stock sync world")
	world.free()
	return packed
