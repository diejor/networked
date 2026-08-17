## Gate suite for the P4a authoritative spawn verbs: [method Netw.replicate]
## and [method Netw.spawn] over the carrier SPAWN/DESPAWN channels.
##
## Pins the I1/I2 invariants (identity and spawn state valid at
## [method Node._enter_tree] on every peer), the spawn-function round trip,
## implicit despawn into [NetwMultiplayerCore], and duplicate-SPAWN
## idempotence.
class_name TestNetwSpawnVerbs
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var probe_scene: PackedScene


func before_test() -> void:
	NetwSpawnProbe.last_despawn_hook_route = 0
	probe_scene = _make_probe_scene()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	client0 = await harness.add_client()
	for mt: MultiplayerTree in [harness.server(), client0]:
		var arena := Node.new()
		arena.name = "Arena"
		mt.add_child(arena)
		var fn_host := NetwSpawnFnHost.new()
		fn_host.name = "FnHost"
		mt.add_child(fn_host)
	await drain_frames(get_tree(), 2)


func test_replicate_stamps_identity_before_tree_on_every_peer() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)

	assert_that(entity).is_not_null()
	assert_int(entity.route).is_greater(0)
	assert_str(String(entity.entity_id)).is_not_empty()

	_server_arena().add_child(node)
	assert_int(node.enter_tree_report["route"]).is_equal(entity.route)

	var client_node := await _wait_live(client0, entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_int(client_node.enter_tree_report["route"]).is_equal(entity.route)
	assert_that(client_node.enter_tree_report["entity_id"]) \
			.is_equal(entity.entity_id)
	assert_int(client_node.ready_report["route"]).is_equal(entity.route)
	assert_that(client_node.get_parent().name).is_equal(&"Arena")
	assert_str(String(client_node.name)).is_equal(String(node.name))


func test_flat_replicate_returns_the_entity_rid() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var api := harness.server().api
	var entity := api.replicate(node)

	assert_bool(entity.is_valid()).is_true()
	assert_object(api.entity_get_node(entity)).is_same(node)
	var route := api.entity_get_route(entity)
	assert_int(route).is_greater(0)
	_server_arena().add_child(node)

	var client_node := await _wait_live(client0, route) as NetwSpawnProbe
	assert_object(client_node).is_not_null()
	assert_int(NetwEntity.of(client_node).route).is_equal(route)


func test_on_spawn_value_applies_before_enter_tree_on_client() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	node.marker = "before-add"
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)
	# The SPAWN frame snapshots at end-of-frame of tree entry, so a value set
	# after add_child in the same frame still rides the frame.
	node.marker = "same-frame"

	var client_node := await _wait_live(client0, entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.enter_tree_report["marker"]).is_equal("same-frame")
	assert_str(client_node.marker).is_equal("same-frame")


# The ME-era regression: synchronizer-style authority derived from peer_id
# inside _enter_tree must observe the stamped identity at registration time,
# on the client, with no pending-spawn window.
func test_owner_stamp_drives_authority_inside_enter_tree() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()
	var owner := harness.server().api.peer_get_participant(peer_id)
	assert_that(owner).is_not_null()

	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node, owner)
	assert_int(entity.peer_id).is_equal(peer_id)
	assert_int(entity.controller).is_equal(peer_id)
	_server_arena().add_child(node)

	var client_node := await _wait_live(client0, entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_int(client_node.enter_tree_report["peer_id"]).is_equal(peer_id)
	assert_int(client_node.enter_tree_report["authority"]).is_equal(peer_id)
	assert_int(client_node.get_multiplayer_authority()).is_equal(peer_id)


func test_spawn_fn_recipe_round_trips_args_and_identity() -> void:
	var host := harness.server().get_node("FnHost") as NetwSpawnFnHost
	var node := _replication().spawn(host._spawn_probe, ["fnmark", 3]) \
			as NetwSpawnProbe
	assert_that(node).is_not_null()
	assert_int(node.fn_tier).is_equal(3)

	var entity := NetwEntity.of(node)
	assert_int(entity.route).is_greater(0)
	_server_arena().add_child(node)

	var client_node := await _wait_live(client0, entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_int(client_node.fn_tier).is_equal(3)
	assert_str(client_node.marker).is_equal("fnmark")
	assert_int(client_node.enter_tree_report["route"]).is_equal(entity.route)
	assert_that(client_node.get_parent().name).is_equal(&"Arena")


func test_fn_registry_recipe_round_trips_host_less() -> void:
	# Every peer registers the same id against its own host method, so the wire
	# carries the id alone with no node anchor.
	for mt: MultiplayerTree in [harness.server(), client0]:
		var host := mt.get_node("FnHost") as NetwSpawnFnHost
		mt.api.spawn_register_constructor(
			&"probe",
			host._spawn_probe,
		)

	var node := _replication()._spawn_pipeline.spawn_registered(
		&"probe",
		["regmark", 5],
	) as NetwSpawnProbe
	assert_that(node).is_not_null()
	assert_int(node.fn_tier).is_equal(5)

	var entity := NetwEntity.of(node)
	assert_int(entity.route).is_greater(0)
	_server_arena().add_child(node)

	var client_node := await _wait_live(client0, entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_int(client_node.fn_tier).is_equal(5)
	assert_str(client_node.marker).is_equal("regmark")
	assert_int(client_node.enter_tree_report["route"]).is_equal(entity.route)
	assert_that(client_node.get_parent().name).is_equal(&"Arena")


func test_despawn_reaches_liveness_and_receiver_hook() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	var route := entity.route
	_server_arena().add_child(node)
	var client_node := await _wait_live(client0, route)
	assert_that(client_node).is_not_null()

	node.queue_free()
	var dead := false
	for i in 60:
		await get_tree().process_frame
		if client0.api.entity_get_state(client0.api.entity_from_route(route)) \
				== NetwMultiplayer.EntityState.DEAD:
			dead = true
			break

	assert_bool(dead).is_true()
	assert_that(harness.server().api.entity_get_state(
			harness.server().api.entity_from_route(route))) \
			.is_equal(NetwMultiplayer.EntityState.DEAD)
	assert_int(NetwSpawnProbe.last_despawn_hook_route).is_equal(route)
	assert_that(client0.api.entity_get_node(
			client0.api.entity_from_route(route))).is_null()


func test_duplicate_spawn_frame_is_idempotent() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)
	var client_node := await _wait_live(client0, entity.route)
	assert_that(client_node).is_not_null()

	var record: NetwSpawnRecord = (
			_replication()._spawn_pipeline._spawn_book.spawned_of(entity.route)
	)
	var payload := _replication()._spawn_pipeline._encode_spawn_frame(record, node)
	var client_replication := client0.api._replication
	var before := int(
		client_replication.counters()[&"drops_spawn_duplicate"],
	)
	client_replication._spawn_pipeline._handle_spawn_frame(payload, 1)

	assert_int(int(client_replication.counters()[&"drops_spawn_duplicate"])) \
			.is_equal(before + 1)
	assert_int(client0.get_node("Arena").get_child_count()).is_equal(1)


func test_double_replicate_warns_and_returns_existing_entity() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var first := _replication().replicate(node)
	var second := _replication().replicate(node)

	assert_bool(first == second).is_true()
	_server_arena().add_child(node)
	var client_node := await _wait_live(client0, first.route)
	assert_that(client_node).is_not_null()


# A spawn-state entry whose target does not resolve on the receiver (a version
# skew or a purely-computed source) must skip exactly its own bytes so every
# later entry in the frame still applies. This drives a hand-built frame whose
# first entry is unresolvable and whose second carries the real root marker.
func test_unresolvable_spawn_state_entry_keeps_later_entries_intact() -> void:
	var pipeline := NetwSpawnPipeline.new(client0.api)
	var route := 99001
	var w := NetwBitBufferWriter.new()
	# SMELL(fault-injection): hand-built SPAWN header, coupled to the private
	# codec. route, entity_id, peer_id, controller, action_spawn_tick,
	# action_requester, netw_table_hash, declares_scene, node_name.
	NetwCodec.put_varint(w, route)
	pipeline._put_str(w, "probe@%d" % route)
	NetwCodec.put_varint(w, 0)
	NetwCodec.put_varint(w, 0)
	NetwCodec.put_varint(w, -1)
	NetwCodec.put_varint(w, 0)
	NetwCodec.put_varint(w, 0)
	# Not a scene, so no stem follows.
	NetwCodec.put_varint(w, 0)
	pipeline._put_str(w, "")
	# Parent anchor: scene structure, the client's Arena node.
	w.put_aligned_u8(0)
	pipeline._put_str(w, "Arena")
	w.put_aligned_u8(NetwSpawnBook.RECIPE_SCENE)
	pipeline._put_scene_recipe(w, probe_scene.resource_path)

	NetwCodec.put_varint(w, 2)
	# Unresolvable entry: a path no child answers to, with junk value bytes.
	w.put_aligned_u8(255)
	pipeline._put_str(w, "Ghost/Missing")
	NetwScriptModel.write_token(w, &"phantom")
	var junk := var_to_bytes("noise")
	NetwCodec.put_varint(w, junk.size())
	w.put_aligned_bytes(junk)
	# Real entry: the root marker, which must survive the skip.
	w.put_aligned_u8(0)
	NetwScriptModel.write_token(w, &"marker")
	var vw := NetwBitBufferWriter.new()
	NetwCodec.write_values(vw, ["recovered"], [null], [TYPE_STRING])
	var vbytes := vw.to_bytes()
	NetwCodec.put_varint(w, vbytes.size())
	w.put_aligned_bytes(vbytes)

	pipeline._handle_spawn_frame(w.to_bytes(), 1)
	await drain_frames(get_tree(), 2)

	var node := client0.api.entity_get_node(
			client0.api.entity_from_route(route)) as NetwSpawnProbe
	assert_that(node).is_not_null()
	assert_str(node.marker).is_equal("recovered")


# A peer that joins after a spawn already exists must receive it through the
# authority's spawn-book replay, not the initial flush it missed.
func test_late_join_replay_hydrates_a_new_peer() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	node.marker = "replayed"
	var entity := _replication().replicate(node)
	var route := entity.route
	# Parent under the tree root so the anchor resolves on any peer without a
	# per-peer Arena setup.
	harness.server().add_child(node)
	var c0_node := await _wait_live(client0, route)
	assert_that(c0_node).is_not_null()

	var client1 := await harness.add_client()
	var c1_node := await _wait_live(client1, route) as NetwSpawnProbe
	assert_that(c1_node).is_not_null()
	assert_int(c1_node.enter_tree_report["route"]).is_equal(route)
	assert_str(c1_node.marker).is_equal("replayed")

	# The despawn must still reach the replayed peer.
	node.queue_free()
	var dead := false
	for i in 90:
		await get_tree().process_frame
		if client1.api.entity_get_state(client1.api.entity_from_route(route)) \
				== NetwMultiplayer.EntityState.DEAD:
			dead = true
			break
	assert_bool(dead).is_true()


func _replication() -> ReplicationCore:
	return harness.server().api._replication


func _server_arena() -> Node:
	return harness.server().get_node("Arena")


func _wait_live(mt: MultiplayerTree, route: int, frames: int = 120) -> Node:
	for i in frames:
		if mt.api.entity_get_state(mt.api.entity_from_route(route)) \
				== NetwMultiplayer.EntityState.LIVE:
			return mt.api.entity_get_node(mt.api.entity_from_route(route))
		await get_tree().process_frame
	return null


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "SpawnVerbProbe"
	var path := NetwPathNamespace.next_path("player", "SpawnVerbProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
