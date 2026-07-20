## Conformance gate for nested [MultiplayerSynchronizer] and
## [MultiplayerSpawner] visibility, reproducing godotengine/godot#68508 with
## stock nodes mounted under [MultiplayerTree].
##
## The cells pin the Networked guarantees missing from native replication:
## nested streams never target an absent ancestor, nested spawns clamp to the
## nearest materialized ancestor, visibility loss and gain apply in topological
## order, nested synchronizers remain independent without a dummy root sync,
## and late-join replay preserves the same rules. Any engine error fails the
## GdUnit case, making the original error-spam symptom part of the contract.
class_name TestNestedVisibilityConformance
extends NetwTestSuite

var harness: NetwTestHarness
var client_a: MultiplayerTree
var child_scene: PackedScene
var nested_sync_scene: PackedScene
var nested_spawner_scene: PackedScene
var dummy_free_scene: PackedScene


func before_test() -> void:
	child_scene = _make_child_scene()
	nested_sync_scene = _make_nested_sync_scene()
	nested_spawner_scene = _make_nested_spawner_scene()
	dummy_free_scene = _make_dummy_free_scene()
	StockNestedVisibilityProbe.child_scene_path = child_scene.resource_path
	StockNestedVisibilityProbe.clear_lifecycle_events()
	StockNestedVisibilityWorld.root_scene_paths = [
		nested_sync_scene.resource_path,
		nested_spawner_scene.resource_path,
		dummy_free_scene.resource_path,
	]
	harness = make_harness()
	await harness.setup(_make_world_scene())
	client_a = await harness.add_client()
	await drain_frames(get_tree(), 2)


func after_test() -> void:
	StockNestedVisibilityProbe.child_scene_path = ""
	StockNestedVisibilityProbe.clear_lifecycle_events()
	StockNestedVisibilityWorld.root_scene_paths = []


# C1: a public nested stream cannot target a peer missing the gated root.
func test_nested_public_sync_never_targets_absent_root() -> void:
	var client_b := await harness.add_client()
	var root := nested_sync_scene.instantiate() \
			as StockNestedVisibilityProbe
	root.name = "NestedSyncRoot"
	var root_sync := _sync(root, "RootSync")
	root_sync.set_visibility_for(_peer_id(client_a), true)
	_arena(harness.server()).add_child(root, true)

	var root_a := await _wait_path(client_a, "NestedSyncRoot") \
			as StockNestedVisibilityProbe
	assert_that(root_a).is_not_null()
	assert_that(await _wait_path(client_b, "NestedSyncRoot", 40)).is_null()

	root.synced_value = 31
	var nested := root.get_node("Nested") as StockNestedVisibilityProbe
	nested.synced_value = 41
	assert_int(await _wait_synced(root_a, 31)).is_equal(31)
	var nested_a := root_a.get_node("Nested") as StockNestedVisibilityProbe
	assert_int(await _wait_synced(nested_a, 41)).is_equal(41)
	assert_that(_path(client_b, "NestedSyncRoot")).is_null()


# C2: children spawned while hidden wait for the parent and then materialize in
# parent-before-child order when the peer gains visibility.
func test_nested_spawner_clamps_children_to_hidden_parent() -> void:
	var client_b := await harness.add_client()
	var root := _spawn_spawner_root("HiddenSpawnerRoot")
	var root_sync := _sync(root, "RootSync")
	root_sync.set_visibility_for(_peer_id(client_a), true)

	var root_a := await _wait_path(client_a, "HiddenSpawnerRoot") \
			as StockNestedVisibilityProbe
	assert_that(root_a).is_not_null()
	assert_that(await _wait_path(client_b, "HiddenSpawnerRoot", 40)).is_null()

	var child := _spawn_child(root, "HiddenChild", 73)
	var child_a := await _wait_path(
		client_a,
		"HiddenSpawnerRoot/Gun/Children/HiddenChild",
	) as StockNestedVisibilityProbe
	assert_that(child_a).is_not_null()
	assert_int(await _wait_synced(child_a, 73)).is_equal(73)
	assert_that(_path(client_b, "HiddenSpawnerRoot")).is_null()

	StockNestedVisibilityProbe.clear_lifecycle_events()
	var peer_b := _peer_id(client_b)
	root_sync.set_visibility_for(peer_b, true)
	root_sync.update_visibility()
	var root_b := await _wait_path(client_b, "HiddenSpawnerRoot")
	var child_b := await _wait_path(
		client_b,
		"HiddenSpawnerRoot/Gun/Children/HiddenChild",
	) as StockNestedVisibilityProbe
	assert_that(root_b).is_not_null()
	assert_that(child_b).is_not_null()
	assert_array(
		StockNestedVisibilityProbe.lifecycle_kinds(peer_b, &"enter"),
	).is_equal([&"parent", &"child"])
	assert_int(await _wait_synced(child_b, child.synced_value)).is_equal(73)


# C3: hiding removes descendants first, and showing replays the parent first.
func test_hide_show_applies_in_topological_order() -> void:
	var client_b := await harness.add_client()
	var root := nested_spawner_scene.instantiate() \
			as StockNestedVisibilityProbe
	root.name = "OrderedRoot"
	var root_sync := _sync(root, "RootSync")
	root_sync.set_visibility_for(_peer_id(client_a), true)
	var peer_b := _peer_id(client_b)
	root_sync.set_visibility_for(peer_b, true)
	_arena(harness.server()).add_child(root, true)
	assert_that(await _wait_path(client_b, "OrderedRoot")).is_not_null()

	var child := _spawn_child(root, "Equipment", 88)
	assert_that(
		await _wait_path(
			client_b,
			"OrderedRoot/Gun/Children/Equipment",
		),
	).is_not_null()
	StockNestedVisibilityProbe.clear_lifecycle_events()

	root_sync.set_visibility_for(peer_b, false)
	root_sync.update_visibility()
	assert_bool(await _wait_absent(client_b, "OrderedRoot")).is_true()
	assert_array(
		StockNestedVisibilityProbe.lifecycle_kinds(peer_b, &"exit"),
	).is_equal([&"child", &"parent"])

	StockNestedVisibilityProbe.clear_lifecycle_events()
	root_sync.set_visibility_for(peer_b, true)
	root_sync.update_visibility()
	var replayed_child := await _wait_path(
		client_b,
		"OrderedRoot/Gun/Children/Equipment",
	) as StockNestedVisibilityProbe
	assert_that(replayed_child).is_not_null()
	assert_array(
		StockNestedVisibilityProbe.lifecycle_kinds(peer_b, &"enter"),
	).is_equal([&"parent", &"child"])
	assert_int(await _wait_synced(replayed_child, child.synced_value)).is_equal(88)


# C4: without a dummy root synchronizer, the root still spawns and each nested
# synchronizer independently honors its own visibility.
func test_nested_syncs_need_no_dummy_root_synchronizer() -> void:
	var client_b := await harness.add_client()
	var root := dummy_free_scene.instantiate() \
			as StockNestedVisibilityProbe
	root.name = "DummyFreeRoot"
	var private_sync := _sync(root.get_node("Private"), "Sync")
	private_sync.set_visibility_for(_peer_id(client_a), true)
	_arena(harness.server()).add_child(root, true)

	var root_a := await _wait_path(client_a, "DummyFreeRoot")
	var root_b := await _wait_path(client_b, "DummyFreeRoot")
	assert_that(root_a).is_not_null()
	assert_that(root_b).is_not_null()
	var public_server := root.get_node("Public") \
			as StockNestedVisibilityProbe
	var private_server := root.get_node("Private") \
			as StockNestedVisibilityProbe
	public_server.synced_value = 11
	private_server.synced_value = 22

	var public_a := root_a.get_node("Public") as StockNestedVisibilityProbe
	var private_a := root_a.get_node("Private") as StockNestedVisibilityProbe
	var public_b := root_b.get_node("Public") as StockNestedVisibilityProbe
	var private_b := root_b.get_node("Private") as StockNestedVisibilityProbe
	assert_int(await _wait_synced(public_a, 11)).is_equal(11)
	assert_int(await _wait_synced(private_a, 22)).is_equal(22)
	assert_int(await _wait_synced(public_b, 11)).is_equal(11)
	await drain_frames(get_tree(), 30)
	assert_int(private_b.synced_value).is_equal(0)

	private_sync.set_visibility_for(_peer_id(client_b), true)
	private_sync.update_visibility()
	assert_int(await _wait_synced(private_b, 22)).is_equal(22)


# C5: late replay applies the same ancestor clamp and parent-first ordering.
func test_late_join_replays_only_admitted_subtree_parent_first() -> void:
	var visible_root := nested_spawner_scene.instantiate() \
			as StockNestedVisibilityProbe
	visible_root.name = "LateVisibleRoot"
	_sync(visible_root, "RootSync").public_visibility = true
	_arena(harness.server()).add_child(visible_root, true)
	var visible_child := _spawn_child(visible_root, "LateVisibleChild", 99)
	var hidden_root := nested_spawner_scene.instantiate() \
			as StockNestedVisibilityProbe
	hidden_root.name = "LateHiddenRoot"
	_arena(harness.server()).add_child(hidden_root, true)
	_spawn_child(hidden_root, "LateHiddenChild", 100)
	await drain_frames(get_tree(), 4)

	StockNestedVisibilityProbe.clear_lifecycle_events()
	var late_client := await harness.add_client()
	var late_peer := _peer_id(late_client)
	var late_root := await _wait_path(late_client, "LateVisibleRoot")
	var late_child := await _wait_path(
		late_client,
		"LateVisibleRoot/Gun/Children/LateVisibleChild",
	) as StockNestedVisibilityProbe
	assert_that(late_root).is_not_null()
	assert_that(late_child).is_not_null()
	assert_that(
		await _wait_path(late_client, "LateHiddenRoot", 40),
	).is_null()
	assert_array(
		StockNestedVisibilityProbe.lifecycle_kinds(late_peer, &"enter"),
	).is_equal([&"parent", &"child"])
	assert_int(
		await _wait_synced(late_child, visible_child.synced_value),
	).is_equal(99)


func _peer_id(tree: MultiplayerTree) -> int:
	return tree.multiplayer_peer.get_unique_id()


func _arena(tree: MultiplayerTree) -> Node:
	return tree.get_node("NestedVisibilityWorld/Arena")


func _path(tree: MultiplayerTree, path: String) -> Node:
	return _arena(tree).get_node_or_null(path)


func _wait_path(
		tree: MultiplayerTree,
		path: String,
		frames: int = 120,
) -> Node:
	for i in frames:
		var found := _path(tree, path)
		if found:
			return found
		await get_tree().process_frame
	return null


func _wait_absent(
		tree: MultiplayerTree,
		path: String,
		frames: int = 120,
) -> bool:
	for i in frames:
		if not _path(tree, path):
			return true
		await get_tree().process_frame
	return false


func _wait_synced(
		probe: StockNestedVisibilityProbe,
		want: int,
		frames: int = 180,
) -> int:
	if not is_instance_valid(probe):
		return -2147483648
	for i in frames:
		if probe.synced_value == want:
			return probe.synced_value
		await get_tree().process_frame
	return probe.synced_value


func _sync(node: Node, name: String) -> MultiplayerSynchronizer:
	return node.get_node(name) as MultiplayerSynchronizer


func _spawn_spawner_root(name: String) -> StockNestedVisibilityProbe:
	var root := nested_spawner_scene.instantiate() \
			as StockNestedVisibilityProbe
	root.name = name
	_arena(harness.server()).add_child(root, true)
	return root


func _spawn_child(
		root: StockNestedVisibilityProbe,
		name: String,
		value: int,
) -> StockNestedVisibilityProbe:
	var child := child_scene.instantiate() as StockNestedVisibilityProbe
	child.name = name
	child.synced_value = value
	root.get_node("Gun/Children").add_child(child, true)
	return child


func _make_child_scene() -> PackedScene:
	var root := StockNestedVisibilityProbe.new()
	root.name = "NestedVisibilityChild"
	root.event_kind = &"child"
	_add_sync(root, root, "Sync", true)
	return _pack_scene(root, "NestedVisibilityChild")


func _make_nested_sync_scene() -> PackedScene:
	var root := StockNestedVisibilityProbe.new()
	root.name = "NestedSyncShape"
	root.event_kind = &"parent"
	_add_sync(root, root, "RootSync", false)
	var nested := StockNestedVisibilityProbe.new()
	nested.name = "Nested"
	root.add_child(nested)
	nested.owner = root
	_add_sync(nested, root, "Sync", true)
	return _pack_scene(root, "NestedSyncShape")


func _make_nested_spawner_scene() -> PackedScene:
	var root := StockNestedVisibilityProbe.new()
	root.name = "NestedSpawnerShape"
	root.event_kind = &"parent"
	_add_sync(root, root, "RootSync", false)
	var gun := Node.new()
	gun.name = "Gun"
	root.add_child(gun)
	gun.owner = root
	var children := Node.new()
	children.name = "Children"
	gun.add_child(children)
	children.owner = root
	var spawner := MultiplayerSpawner.new()
	spawner.name = "ChildSpawner"
	spawner.spawn_path = NodePath("../Children")
	gun.add_child(spawner)
	spawner.owner = root
	return _pack_scene(root, "NestedSpawnerShape")


func _make_dummy_free_scene() -> PackedScene:
	var root := StockNestedVisibilityProbe.new()
	root.name = "DummyFreeShape"
	root.event_kind = &"parent"
	var public_node := StockNestedVisibilityProbe.new()
	public_node.name = "Public"
	root.add_child(public_node)
	public_node.owner = root
	_add_sync(public_node, root, "Sync", true)
	var private_node := StockNestedVisibilityProbe.new()
	private_node.name = "Private"
	root.add_child(private_node)
	private_node.owner = root
	_add_sync(private_node, root, "Sync", false)
	return _pack_scene(root, "DummyFreeShape")


func _add_sync(
		target: StockNestedVisibilityProbe,
		owner: Node,
		name: String,
		public_visibility: bool,
) -> MultiplayerSynchronizer:
	var sync := MultiplayerSynchronizer.new()
	sync.name = name
	sync.root_path = NodePath("..")
	sync.public_visibility = public_visibility
	var config := SceneReplicationConfig.new()
	var property := NodePath(".:synced_value")
	config.add_property(property)
	config.property_set_replication_mode(
		property,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = config
	target.add_child(sync)
	sync.owner = owner
	return sync


func _pack_scene(root: Node, stem: String) -> PackedScene:
	var path := NetwPathNamespace.next_path("sync", stem)
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _make_world_scene() -> PackedScene:
	var world := StockNestedVisibilityWorld.new()
	world.name = "NestedVisibilityWorld"
	var arena := Node.new()
	arena.name = "Arena"
	world.add_child(arena)
	arena.owner = world
	var spawner := MultiplayerSpawner.new()
	spawner.name = "RootSpawner"
	spawner.spawn_path = NodePath("../Arena")
	world.add_child(spawner)
	spawner.owner = world
	var packed := PackedScene.new()
	var err := packed.pack(world)
	assert(err == OK, "failed to pack nested visibility world")
	world.free()
	return packed
