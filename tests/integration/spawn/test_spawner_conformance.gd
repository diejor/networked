## Conformance gate for [MultiplayerSpawner] consumption: an unmodified stock
## project (plain spawner, plain [MultiplayerSynchronizer] spawn state, no
## Networked nodes) mounted under a [MultiplayerTree] must keep working row by
## row against the native contract.
##
## Pins the [NetwSpawnerCompat] adapter: scene-list auto-spawn, custom
## [method MultiplayerSpawner.spawn] data recovery through the spawn-function
## wrap, native replication_config spawn state applied pre-tree, spawner
## signals on receivers, spawn_limit, late-join replay, and the G1
## double-spawn guard.
class_name TestSpawnerConformance
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var probe_scene: PackedScene


func before_test() -> void:
	probe_scene = _make_probe_scene()
	StockSpawnProbe.packed_scene_path = probe_scene.resource_path
	harness = make_harness()
	await harness.setup(_make_world_scene())
	client0 = await harness.add_client()
	await drain_frames(get_tree(), 2)


func after_test() -> void:
	StockSpawnProbe.packed_scene_path = ""


func test_scene_list_auto_spawn_replicates_stock_state() -> void:
	var node := probe_scene.instantiate() as StockSpawnProbe
	node.stock_value = "stocked"
	node.name = "StockChild"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "StockChild") \
			as StockSpawnProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.stock_value).is_equal("stocked")
	# Strengthening over native: spawn state is applied before _enter_tree.
	assert_str(client_node.enter_tree_value).is_equal("stocked")


# Row 14: native reads spawn state after the authority's ready, so a value
# mutated inside _ready must still ride the frame. Our end-of-frame snapshot
# captures the same values.
func test_spawn_state_mutated_in_ready_is_captured() -> void:
	var node := probe_scene.instantiate() as StockSpawnProbe
	node.stock_value = "pre-ready"
	node.mutate_in_ready = true
	node.name = "ReadyMutated"
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "ReadyMutated") \
			as StockSpawnProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.stock_value).is_equal("ready-mutated")


func test_custom_spawn_function_round_trips_data() -> void:
	var spawner := _spawner(harness.server())
	var node := spawner.spawn("payload-42") as StockSpawnProbe
	assert_that(node).is_not_null()
	assert_str(node.custom_data).is_equal("payload-42")

	var client_node := await _wait_arena_child(client0, String(node.name)) \
			as StockSpawnProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.custom_data).is_equal("payload-42")


func test_spawner_signals_fire_on_receiver() -> void:
	var spawned_nodes: Array[Node] = []
	var despawned_nodes: Array[Node] = []
	var client_spawner := _spawner(client0)
	client_spawner.spawned.connect(func(n: Node) -> void:
		spawned_nodes.append(n))
	client_spawner.despawned.connect(func(n: Node) -> void:
		despawned_nodes.append(n))

	var node := probe_scene.instantiate() as StockSpawnProbe
	node.name = "Signaled"
	_arena(harness.server()).add_child(node, true)
	var client_node := await _wait_arena_child(client0, "Signaled")
	assert_that(client_node).is_not_null()
	assert_int(spawned_nodes.size()).is_equal(1)
	assert_bool(spawned_nodes[0] == client_node).is_true()

	node.queue_free()
	var gone := false
	for i in 90:
		await get_tree().process_frame
		if not is_instance_valid(client_node) \
				or not client_node.is_inside_tree():
			gone = true
			break
	assert_bool(gone).is_true()
	assert_int(despawned_nodes.size()).is_equal(1)


# Receiver-side spawn_limit, the half our compat adapter enforces (native
# checks it inside instantiate_scene/instantiate_custom on receivers too).
# The authority-side half stays native inside MultiplayerSpawner.spawn and
# raises an engine error when exceeded, so it is not driven here. The probe
# is synchronizer-free because a server whose peer silently refused a spawn
# keeps path-confirming its synchronizers, native-conformant noise this case
# does not pin.
func test_spawn_limit_blocks_excess_spawns_on_receiver() -> void:
	var bare := _make_bare_probe_scene()
	_spawner(harness.server()).add_spawnable_scene(bare.resource_path)
	_spawner(client0).add_spawnable_scene(bare.resource_path)
	_spawner(client0).spawn_limit = 1
	for i in 2:
		var node := bare.instantiate()
		node.name = "Limited%d" % i
		_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "Limited0")
	assert_that(client_node).is_not_null()
	await drain_frames(get_tree(), 20)
	assert_int(_arena(harness.server()).get_child_count()).is_equal(2)
	assert_int(_arena(client0).get_child_count()).is_equal(1)


func test_late_joiner_receives_stock_spawns() -> void:
	var node := probe_scene.instantiate() as StockSpawnProbe
	node.stock_value = "replayed"
	node.name = "Replayed"
	_arena(harness.server()).add_child(node, true)
	var c0_node := await _wait_arena_child(client0, "Replayed")
	assert_that(c0_node).is_not_null()

	var client1 := await harness.add_client()
	var c1_node := await _wait_arena_child(client1, "Replayed") \
			as StockSpawnProbe
	assert_that(c1_node).is_not_null()
	assert_str(c1_node.stock_value).is_equal("replayed")


# G1: a verb-replicated node placed under a watched spawn path triggers a
# spawner registration for a node already in the books. The registration is
# consumed and ignored, so exactly one node materializes on the client.
func test_verb_replicated_node_under_spawn_path_spawns_once() -> void:
	var node := probe_scene.instantiate() as StockSpawnProbe
	node.name = "VerbSpawned"
	var entity := harness.server().api.replication.replicate(node)
	assert_that(entity).is_not_null()
	_arena(harness.server()).add_child(node, true)

	var client_node := await _wait_arena_child(client0, "VerbSpawned")
	assert_that(client_node).is_not_null()
	assert_int(_arena(client0).get_child_count()).is_equal(1)


func _arena(mt: MultiplayerTree) -> Node:
	return mt.get_node("StockWorld/Arena")


func _spawner(mt: MultiplayerTree) -> MultiplayerSpawner:
	return mt.get_node("StockWorld/StockSpawner") as MultiplayerSpawner


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


func _make_probe_scene() -> PackedScene:
	var root := StockSpawnProbe.new()
	root.name = "StockProbe"
	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var prop := NodePath(".:stock_value")
	cfg.add_property(prop)
	cfg.property_set_spawn(prop, true)
	cfg.property_set_replication_mode(
		prop,
		SceneReplicationConfig.REPLICATION_MODE_NEVER,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root
	var path := NetwPathNamespace.next_path("player", "StockProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _make_bare_probe_scene() -> PackedScene:
	var root := StockSpawnProbe.new()
	root.name = "BareProbe"
	var path := NetwPathNamespace.next_path("player", "BareProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _make_world_scene() -> PackedScene:
	var world := StockWorld.new()
	world.name = "StockWorld"
	var arena := Node.new()
	arena.name = "Arena"
	world.add_child(arena)
	arena.owner = world
	var spawner := MultiplayerSpawner.new()
	spawner.name = "StockSpawner"
	spawner.spawn_path = NodePath("../Arena")
	world.add_child(spawner)
	spawner.owner = world
	var packed := PackedScene.new()
	var err := packed.pack(world)
	assert(err == OK, "failed to pack the stock world")
	world.free()
	return packed
