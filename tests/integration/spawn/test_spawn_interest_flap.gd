## Gate suite for spawn-pipeline behavior under interest flapping: the
## DEAD-route revival on re-admission, the DEAD-anchor dependency park, the
## park-expiry unparking that keeps a timed-out route from swallowing a later
## DESPAWN, the REPARENT recipient filter that keeps a peer losing visibility
## off the reparent edge, and server/client spawn-book convergence after each
## flap settles.
class_name TestSpawnInterestFlap
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


func test_interest_readmission_revives_route() -> void:
	var peer0 := client0.multiplayer_peer.get_unique_id()
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	var route := entity.route
	harness.server().get_node("Arena").add_child(node)

	var interest := harness.server().api.interest
	var layer := interest.layer_for(&"flap")
	layer.add_entity(entity)
	interest.flush()
	await drain_frames(get_tree(), 5)
	assert_that(client0.api.liveness.route_state(route)) \
			.is_equal(NetwLivenessInterface.State.UNKNOWN)

	layer.add_viewer(peer0)
	interest.flush()
	assert_that(await _wait_state(
		client0, route, NetwLivenessInterface.State.LIVE,
	)).is_true()
	_assert_books_converged(route, [client0])

	layer.remove_viewer(peer0)
	interest.flush()
	assert_that(await _wait_state(
		client0, route, NetwLivenessInterface.State.DEAD,
	)).is_true()
	_assert_books_converged(route, [client0])

	layer.add_viewer(peer0)
	interest.flush()
	assert_that(await _wait_state(
		client0, route, NetwLivenessInterface.State.LIVE,
	)).is_true()
	_assert_books_converged(route, [client0])


func test_spawn_parked_on_dead_anchor_applies_on_revival() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	assert_that(await _wait_live(client0, child_route)).is_not_null()
	var frames := _reencode_frames(spawned)

	var replication := client0.api.replication
	var pipeline := replication._spawn_pipeline
	pipeline._handle_despawn_frame(_despawn_payload(child_route), 1)
	pipeline._handle_despawn_frame(_despawn_payload(parent_route), 1)
	await drain_frames(get_tree(), 2)
	assert_that(client0.api.liveness.route_state(parent_route)) \
			.is_equal(NetwLivenessInterface.State.DEAD)
	assert_that(client0.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.DEAD)

	var deferrals := int(replication.counters()[&"spawn_deferrals"])
	var unresolved := int(replication.counters()[&"drops_spawn_unresolved"])
	pipeline._handle_spawn_frame(frames["child"], 1)
	assert_int(int(replication.counters()[&"spawn_deferrals"])) \
			.is_equal(deferrals + 1)
	assert_int(int(replication.counters()[&"drops_spawn_unresolved"])) \
			.is_equal(unresolved)

	pipeline._handle_spawn_frame(frames["parent"], 1)
	assert_that(client0.api.liveness.route_state(parent_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	assert_that(client0.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	var parent_node := client0.api.liveness.node_of(parent_route)
	assert_that(client0.api.liveness.node_of(child_route).get_parent()) \
			.is_equal(parent_node)


func test_park_expiry_unparks_route_and_heals() -> void:
	var spawned := _spawn_nested_pair()
	var parent_route: int = spawned["parent_entity"].route
	var child_route: int = spawned["child_entity"].route
	assert_that(await _wait_live(client0, child_route)).is_not_null()

	var client1 := await harness.add_client()
	_mount_rig(client1)
	await drain_frames(get_tree(), 2)

	var frames := _reencode_frames(spawned)
	var replication := client1.api.replication
	var pipeline := replication._spawn_pipeline
	pipeline.park_timeout_seconds = 0.05
	pipeline._handle_spawn_frame(frames["child"], 1)
	assert_bool(pipeline._parked_spawn_routes.has(child_route)).is_true()

	var expired := false
	for i in 300:
		await get_tree().process_frame
		if int(replication.counters()[&"spawn_park_expired"]) > 0:
			expired = true
			break
	assert_bool(expired).is_true()
	assert_bool(pipeline._parked_spawn_routes.is_empty()).is_true()

	# A DESPAWN after the expiry must not be swallowed by a stale park flag,
	# which is exactly the leak that kept a later revived node from freeing.
	var cancelled := int(replication.counters()[&"spawn_parked_cancelled"])
	var unknown := int(replication.counters()[&"drops_despawn_unknown"])
	pipeline._handle_despawn_frame(_despawn_payload(child_route), 1)
	assert_int(int(replication.counters()[&"spawn_parked_cancelled"])) \
			.is_equal(cancelled)
	assert_int(int(replication.counters()[&"drops_despawn_unknown"])) \
			.is_equal(unknown + 1)

	pipeline._handle_spawn_frame(frames["parent"], 1)
	pipeline._handle_spawn_frame(frames["child"], 1)
	assert_that(client1.api.liveness.route_state(parent_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)
	assert_that(client1.api.liveness.route_state(child_route)) \
			.is_equal(NetwLivenessInterface.State.LIVE)


func test_reparent_skips_peer_losing_visibility() -> void:
	var client1 := await harness.add_client()
	_mount_rig(client1)
	await drain_frames(get_tree(), 2)
	var peer0 := client0.multiplayer_peer.get_unique_id()
	var peer1 := client1.multiplayer_peer.get_unique_id()
	var interest := harness.server().api.interest

	var zone1 := _spawn_zone(&"zone1", "Zone1", [peer0, peer1])
	var zone2 := _spawn_zone(&"zone2", "Zone2", [peer0])
	var zone1_route: int = NetwEntity.of(zone1).route
	var zone2_route: int = NetwEntity.of(zone2).route
	assert_that(await _wait_live(client0, zone2_route)).is_not_null()
	assert_that(await _wait_live(client1, zone1_route)).is_not_null()

	var child := probe_scene.instantiate() as NetwSpawnProbe
	child.name = "Mover"
	var child_route := _replication().replicate(child).route
	zone1.add_child(child)
	assert_that(await _wait_live(client0, child_route)).is_not_null()
	assert_that(await _wait_live(client1, child_route)).is_not_null()

	child.get_parent().remove_child(child)
	zone2.add_child(child)
	interest.flush()

	var moved := false
	for i in 120:
		await get_tree().process_frame
		var c0_child := client0.api.liveness.node_of(child_route)
		if c0_child and c0_child.get_parent() \
				== client0.api.liveness.node_of(zone2_route) \
				and client1.api.liveness.route_state(child_route) \
				== NetwLivenessInterface.State.DEAD:
			moved = true
			break
	assert_bool(moved).is_true()

	# The losing peer must get the sweep's DESPAWN and never a REPARENT
	# anchored on a route it was not sent, so nothing is left waiting.
	await drain_frames(get_tree(), 5)
	assert_int(client1.api.liveness.pending_live_count()).is_equal(0)
	_assert_books_converged(child_route, [client0, client1])


func test_reparent_reanchors_consumed_spawner_recipe() -> void:
	var client1 := await harness.add_client()
	_mount_rig(client1)
	await drain_frames(get_tree(), 2)
	var peer0 := client0.multiplayer_peer.get_unique_id()
	var peer1 := client1.multiplayer_peer.get_unique_id()

	var zone_scene := _make_zone_scene()
	var zone1 := _spawn_zone(&"zoneA", "ZoneA", [peer0], zone_scene)
	var zone2 := _spawn_zone(&"zoneB", "ZoneB", [peer0, peer1], zone_scene)
	var zone1_route: int = NetwEntity.of(zone1).route
	var zone2_route: int = NetwEntity.of(zone2).route
	assert_that(await _wait_live(client0, zone1_route)).is_not_null()
	assert_that(await _wait_live(client1, zone2_route)).is_not_null()

	# A spawner-consumed spawn inside ZoneA, a scene peer1 was never admitted
	# to, so its recipe anchors on a route peer1 can never resolve.
	var mover := probe_scene.instantiate() as NetwSpawnProbe
	mover.name = "SpawnerMover"
	zone1.add_child(mover, true)
	var mover_route: int = NetwEntity.of(mover).route
	assert_int(mover_route).is_greater(0)
	assert_that(await _wait_live(client0, mover_route)).is_not_null()
	assert_that(client1.api.liveness.route_state(mover_route)) \
			.is_equal(NetwLivenessInterface.State.UNKNOWN)

	var unresolved := int(
		client1.api.replication.counters()[&"drops_spawn_unresolved"]
	)
	mover.get_parent().remove_child(mover)
	zone2.add_child(mover)

	var record: NetwSpawnBook.SpawnRecord = \
			_replication()._spawn_pipeline._spawn_book.spawned[mover_route]
	var c1_mover := await _wait_live(client1, mover_route)
	assert_that(c1_mover).is_not_null()
	assert_that(c1_mover.get_parent()) \
			.is_equal(client1.api.liveness.node_of(zone2_route))
	assert_that(record.spawner()).is_equal(zone2.get_node("Spawner"))
	assert_int(int(
		client1.api.replication.counters()[&"drops_spawn_unresolved"]
	)).is_equal(unresolved)

	await drain_frames(get_tree(), 5)
	assert_int(client1.api.liveness.pending_live_count()).is_equal(0)
	_assert_books_converged(mover_route, [client0, client1])


func _spawn_zone(
		layer_id: StringName,
		zone_name: String,
		viewers: Array[int],
		zone_scene: PackedScene = null,
) -> Node:
	var source := zone_scene if zone_scene else probe_scene
	var node := source.instantiate() as NetwSpawnProbe
	node.name = zone_name
	var entity := _replication().replicate(node)
	harness.server().get_node("Arena").add_child(node)
	var interest := harness.server().api.interest
	var layer := interest.layer_for(layer_id)
	layer.add_entity(entity)
	for peer_id in viewers:
		layer.add_viewer(peer_id)
	interest.flush()
	return node


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


func _despawn_payload(route: int) -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, route)
	return w.to_bytes()


# Asserts the per-peer books agree: a peer is in the server record's
# recipients exactly when its own recv book holds the route.
func _assert_books_converged(route: int, clients: Array) -> void:
	var record: NetwSpawnBook.SpawnRecord = \
			_replication()._spawn_pipeline._spawn_book.spawned.get(route)
	assert_that(record).is_not_null()
	for mt: MultiplayerTree in clients:
		var peer_id := mt.multiplayer_peer.get_unique_id()
		var has_route: bool = \
				mt.api.replication._spawn_pipeline._spawn_book.is_recv(route)
		assert_bool(has_route).is_equal(peer_id in record.recipients)


func _mount_rig(mt: MultiplayerTree) -> void:
	var arena := Node.new()
	arena.name = "Arena"
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


func _wait_state(
		mt: MultiplayerTree,
		route: int,
		state: NetwLivenessInterface.State,
		frames: int = 120,
) -> bool:
	for i in frames:
		if mt.api.liveness.route_state(route) == state:
			return true
		await get_tree().process_frame
	return false


# A zone scene with an embedded MultiplayerSpawner watching the zone root,
# the quick_start level shape that consumes player spawns per scene.
func _make_zone_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "FlapZone"
	var spawner := MultiplayerSpawner.new()
	spawner.name = "Spawner"
	spawner.spawn_path = NodePath("..")
	spawner.add_spawnable_scene(probe_scene.resource_path)
	root.add_child(spawner)
	spawner.owner = root
	var path := NetwPathNamespace.next_path("player", "FlapZone")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "FlapProbe"
	var path := NetwPathNamespace.next_path("player", "FlapProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
