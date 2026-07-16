## Tests for [NetwEntity] identity, spawn-envelope, and controller lifecycle
## behavior.
class_name TestNetwEntityIdentity
extends NetwTestSuite

func test_identity_name_parsing() -> void:
	var valid_rows := [
		["valeria|42", 42, &"valeria"],
		["player|2147483647", 2147483647, &"player"],
	]
	for row in valid_rows:
		assert_that(NetwEntity.parse_peer(row[0])).is_equal(row[1])
		assert_that(NetwEntity.parse_entity(row[0])).is_equal(row[2])

	for name in ["no_separator", "", "|", "a|b|c", "user|abc"]:
		assert_that(NetwEntity.parse_peer(name)).is_equal(0)
	if NetwEntity.parse_entity("|") != &"":
		assert_that(NetwEntity.parse_entity("|")).is_equal(&"")
	assert_that(NetwEntity.parse_entity("a|b|c")).is_equal(&"")


func test_bind_and_spawn_identity_envelope() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	NetwEntity.bind(root, &"valeria", 42)

	var entity := NetwEntity.of(root)
	assert_that(root.name).is_equal("valeria|42")
	assert_that(entity.entity_id).is_equal(&"valeria")
	assert_that(entity.peer_id).is_equal(42)

	var rj := ResolvedJoin.new()
	rj.username = &"valeria"
	rj.peer_id = 42
	var source := {
		"spawn_index": 7,
	}

	var data := NetwSpawn.decorate_spawn(source, rj)
	var netw: Dictionary = data["_netw"]
	var spawn_identity := NetwSpawn.spawn_identity(data)

	assert_that(NetwSpawn._spawn_identity_error(data)).is_empty()
	assert_that(NetwSpawn._is_spawn_envelope(NetwSpawn._spawn_envelope(rj))) \
			.is_true()
	assert_that(NetwSpawn._is_spawn_envelope({ })).is_false()
	assert_that(
		NetwSpawn._is_spawn_envelope(
			{
				"_netw": { "entity_id": "", "peer_id": 42 },
				"data": { },
			},
		),
	).is_false()
	assert_that(source.has("_netw")).is_false()
	assert_that(data["spawn_index"]).is_equal(7)
	assert_that(netw["entity_id"]).is_equal(&"valeria")
	assert_that(netw["peer_id"]).is_equal(42)
	assert_that(spawn_identity.entity_id).is_equal(&"valeria")
	assert_that(spawn_identity.peer_id).is_equal(42)


func test_wrap_spawn_binds_identity_and_strips_envelope() -> void:
	var rj := ResolvedJoin.new()
	rj.username = &"valeria"
	rj.peer_id = 42
	var payload := PackedByteArray([1, 2, 3])
	var envelope := NetwSpawn._spawn_envelope(rj, payload)
	var received: Array[Variant] = []
	var wrapped := NetwSpawn.wrap_spawn(
		func(spawn_payload: Variant) -> Node:
			received.append(spawn_payload)
			var player := Node2D.new()
			player.name = "Player"
			return player
	)

	var player := wrapped.call(envelope) as Node
	auto_free(player)
	var entity := NetwEntity.of(player)
	var clean := received[0] as PackedByteArray

	assert_that(wrapped.get_method()).is_equal(&"_wrapped_spawn")
	assert_that(clean).is_equal(payload)
	assert_that(player.name).is_equal("valeria|42")
	assert_that(entity.entity_id).is_equal(&"valeria")
	assert_that(entity.peer_id).is_equal(42)


func test_controller_lifecycle_flow() -> void:
	var root := _make_player_root(42)
	var entity := NetwEntity.of(root)
	entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
	add_child(root)

	assert_that(root.get_multiplayer_authority()).is_equal(42)
	assert_that(entity.controller).is_equal(42)
	assert_that(NetwEntity.of(root).controller).is_equal(42)
	assert_that(NetwEntity.of(root).control_kind) \
			.is_equal(NetwEntity.ControlKind.PEER_CONTROLLED)

	var server_root := _make_player_root(42)
	var server_entity := NetwEntity.of(server_root)
	server_entity.initial_controller = NetwEntity.InitialController.SERVER
	add_child(server_root)
	assert_that(server_root.get_multiplayer_authority()).is_equal(1)

	var no_peer_root: Node2D = auto_free(Node2D.new())
	no_peer_root.name = "NoSeparator"
	var no_peer_entity := NetwEntity.ensure(no_peer_root)
	no_peer_entity.initial_controller = NetwEntity.InitialController.REPRESENTED_PEER
	add_child(no_peer_root)
	assert_that(no_peer_root.get_multiplayer_authority()).is_equal(1)

	var grant_root := _make_player_root(0)
	var grant_entity := NetwEntity.of(grant_root)
	add_child(grant_root)
	grant_entity.grant_control(42)

	assert_that(grant_root.get_multiplayer_authority()).is_equal(42)
	assert_that(grant_entity.controller).is_equal(42)
	assert_that(grant_entity.control_kind) \
			.is_equal(NetwEntity.ControlKind.PEER_CONTROLLED)

	grant_entity.revoke_control()

	assert_that(grant_root.get_multiplayer_authority()).is_equal(1)
	assert_that(grant_entity.controller).is_equal(0)
	assert_that(grant_entity.control_kind) \
			.is_equal(NetwEntity.ControlKind.SERVER_CONTROLLED)

	var validator := TopologyValidator.new()
	grant_entity.grant_control(42)
	assert_that(validator._get_expected_authority(grant_root)).is_equal(42)
	grant_entity.revoke_control()
	assert_that(validator._get_expected_authority(grant_root)).is_equal(1)


func test_entity_lookup_and_resolution_flow() -> void:
	var root: Node2D = auto_free(Node2D.new())
	NetwEntity.ensure(root)

	var child := Node2D.new()
	root.add_child(child)
	auto_free(child)
	var child_entity := NetwEntity.ensure(child)
	assert_that(child_entity).is_not_null()
	assert_that(child.has_meta(NetwEntity._META_KEY)).is_true()
	assert_that(NetwEntity.of(child)).is_equal(child_entity)

	var clean := Node2D.new()
	auto_free(clean)
	assert_that(NetwEntity.of(clean)).is_null()
	var clean_entity := NetwEntity.ensure(clean)
	assert_that(NetwEntity.of(clean)).is_equal(clean_entity)

	var parent := Node2D.new()
	var orphan_child := Node2D.new()
	parent.add_child(orphan_child)
	auto_free(parent)
	auto_free(orphan_child)

	var resolved := NetwEntity.resolve(orphan_child)
	assert_that(resolved).is_not_null()
	assert_that(NetwEntity.of(parent)).is_equal(resolved)
	assert_that(NetwEntity.of(orphan_child)).is_equal(resolved)

	var live_parent := Node2D.new()
	var live_child := Node2D.new()
	live_parent.add_child(live_child)
	add_child(live_parent)
	auto_free(live_parent)
	auto_free(live_child)

	assert_that(NetwEntity.resolve(live_child)).is_null()


func test_scene_tracking_requires_own_entity_record() -> void:
	var scene := MultiplayerScene.new()
	scene.name = "Arena"
	auto_free(scene)
	scene.gate = auto_free(InterestGate.new())

	var parent := Node2D.new()
	var child := Node2D.new()
	child.name = "Projectile"
	parent.add_child(child)
	auto_free(parent)
	auto_free(child)

	NetwEntity.ensure(parent)

	@warning_ignore("redundant_await")
	await assert_error(
		func() -> void:
			scene.track_node(child)
	).is_success()

	assert_that(scene.tracked_nodes.has(child)).is_false()

	NetwEntity.ensure(child)
	scene.track_node(child)

	assert_that(scene.tracked_nodes.has(child)).is_true()


func _make_player_root(peer_id: int) -> Node2D:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria|%d" % peer_id
	NetwEntity.ensure(root)
	return root


func test_envelope_route_binds_before_tree_entry() -> void:
	var mt := MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	var liveness := mt.api.liveness

	# 1. Reserve a route on the server
	var route := liveness.reserve_route()
	assert_that(route).is_greater(0)

	# 2. Build spawn envelope
	var rj := ResolvedJoin.new()
	rj.username = &"player1"
	rj.peer_id = 42
	var envelope := NetwSpawn._spawn_envelope(rj, null, route)

	# 3. Simulate wrap_spawn callback
	var spawn_identity := NetwSpawn.spawn_identity(envelope)

	var root := Node2D.new()
	root.name = "player1"
	auto_free(root)

	# Bind identity and the reserved route BEFORE entering the tree
	spawn_identity.bind(root)

	# Add to the tree to trigger tree-entry route binding
	mt.add_child(root)

	# 4. Verify that route is bound correctly
	var entity := NetwEntity.of(root)
	assert_that(liveness.route_of(entity)).is_equal(route)
	assert_that(liveness.entity_of(route)).is_equal(entity)
