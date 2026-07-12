## Gate suite for the P4b lifecycle edges: nested entity spawns, the
## dependency park for child-before-parent delivery, DESPAWN cancelling a
## parked SPAWN, the child-first despawn cascade, and the end-of-frame
## reparent grace that keeps a route alive across remove_child + add_child.
class_name TestNetwSpawnNesting
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var probe_scene: PackedScene


func before_test() -> void:
	probe_scene = _make_probe_scene()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	client0 = await harness.add_client()
	_mount_rig(harness.server())
	_mount_rig(client0)
	await drain_frames(get_tree(), 2)


func test_nested_child_spawns_under_parent_entity() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route

	var client_parent := await _wait_live(client0, parent_route)
	var client_child := await _wait_live(client0, child_route)

	assert_that(client_parent).is_not_null()
	assert_that(client_child).is_not_null()
	assert_that(client_child.get_parent()).is_equal(client_parent)


func test_child_frame_before_parent_parks_then_applies() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	assert_that(await _wait_live(client0, child_route)).is_not_null()

	var client1 := await harness.add_client()
	_mount_rig(client1)
	await drain_frames(get_tree(), 2)

	var frames := _reencode_frames(spawned)
	var replication := client1.api.replication
	var deferrals := int(replication.counters()[&"spawn_deferrals"])

	replication._spawn_pipeline._handle_spawn_frame(frames["child"], 1)
	assert_int(int(replication.counters()[&"spawn_deferrals"])) \
			.is_equal(deferrals + 1)
	assert_that(client1.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.UNKNOWN)

	replication._spawn_pipeline._handle_spawn_frame(frames["parent"], 1)
	assert_that(client1.api.liveness.route_state(parent_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	assert_that(client1.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	var parent_node := client1.api.liveness.node_of(parent_route)
	assert_that(client1.api.liveness.node_of(child_route).get_parent()) \
			.is_equal(parent_node)


func test_despawn_cancels_parked_spawn() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	assert_that(await _wait_live(client0, child_route)).is_not_null()

	var client1 := await harness.add_client()
	_mount_rig(client1)
	await drain_frames(get_tree(), 2)

	var frames := _reencode_frames(spawned)
	var replication := client1.api.replication
	replication._spawn_pipeline._handle_spawn_frame(frames["child"], 1)

	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, child_route)
	var cancelled := int(replication.counters()[&"spawn_parked_cancelled"])
	replication._spawn_pipeline._handle_despawn_frame(w.to_bytes(), 1)
	assert_int(int(replication.counters()[&"spawn_parked_cancelled"])) \
			.is_equal(cancelled + 1)

	replication._spawn_pipeline._handle_spawn_frame(frames["parent"], 1)
	var parent_node := client1.api.liveness.node_of(parent_route)
	assert_that(parent_node).is_not_null()
	assert_int(parent_node.get_child_count()).is_equal(0)
	assert_that(client1.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.UNKNOWN)


func test_reparent_keeps_route_and_instance() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	var route := entity.route
	harness.server().get_node("Arena").add_child(node)
	var client_node := await _wait_live(client0, route)
	assert_that(client_node).is_not_null()

	node.get_parent().remove_child(node)
	harness.server().get_node("Arena2").add_child(node)

	var moved := false
	for i in 60:
		await get_tree().process_frame
		if is_instance_valid(client_node) and client_node.get_parent() \
				and client_node.get_parent().name == &"Arena2":
			moved = true
			break

	assert_bool(moved).is_true()
	assert_that(harness.server().api.liveness.route_state(route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	assert_that(client0.api.liveness.route_state(route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	assert_that(client0.api.liveness.node_of(route)).is_equal(client_node)


func test_parent_despawn_cascades_without_unknown_drops() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	assert_that(await _wait_live(client0, child_route)).is_not_null()

	var drops := int(
		client0.api.replication.counters()[&"drops_despawn_unknown"]
	)
	(spawned["parent_node"] as Node).queue_free()

	var both_dead := false
	for i in 60:
		await get_tree().process_frame
		var liveness := client0.api.liveness
		if liveness.route_state(parent_route) \
				== NetwLivenessInterface.State.DEAD \
				and liveness.route_state(child_route) \
				== NetwLivenessInterface.State.DEAD:
			both_dead = true
			break

	assert_bool(both_dead).is_true()
	assert_that(harness.server().api.liveness.route_state(parent_route)) \
			.is_equal(NetwLivenessInterface.State.DEAD)
	assert_that(harness.server().api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.DEAD)
	assert_int(int(
		client0.api.replication.counters()[&"drops_despawn_unknown"]
	)).is_equal(drops)


func _spawn_nested_pair() -> Dictionary:
	var parent_node := probe_scene.instantiate() as NetwSpawnProbe
	var parent_entity := _replication().replicate(parent_node)
	harness.server().get_node("Arena").add_child(parent_node)
	var child_node := probe_scene.instantiate() as NetwSpawnProbe
	var child_entity := _replication().replicate(child_node)
	parent_node.add_child(child_node)
	return {
		"parent_node": parent_node,
		"parent_entity": parent_entity,
		"child_node": child_node,
		"child_entity": child_entity,
	}


# Re-encodes the pair's SPAWN frames from the server's spawn book, so a suite
# can drive a receive path directly in a chosen order.
func _reencode_frames(spawned: Dictionary) -> Dictionary:
	var replication := _replication()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	return {
		"parent": replication._spawn_pipeline._encode_spawn_frame(
			replication._spawn_pipeline._spawn_book.spawned[parent_route],
			spawned["parent_node"],
		),
		"child": replication._spawn_pipeline._encode_spawn_frame(
			replication._spawn_pipeline._spawn_book.spawned[child_route],
			spawned["child_node"],
		),
	}


func _mount_rig(mt: MultiplayerTree) -> void:
	for arena_name in ["Arena", "Arena2"]:
		var arena := Node.new()
		arena.name = arena_name
		mt.add_child(arena)


func _replication() -> NetwReplicationInterface:
	return harness.server().api.replication


func _wait_live(mt: MultiplayerTree, route: int, frames: int = 120) -> Node:
	for i in frames:
		if mt.api.liveness.route_state(route) \
				== NetwLivenessInterface.State.LIVE:
			return mt.api.liveness.node_of(route)
		await get_tree().process_frame
	return null


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "NestProbe"
	var path := NetwPathNamespace.next_path("player", "NestProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
