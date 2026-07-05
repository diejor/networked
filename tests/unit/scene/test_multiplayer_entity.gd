## Tests for [MultiplayerEntity].
##
## Covers [NetwEntity] identity helpers, spawn-property collection, and
## [enum MultiplayerEntity.InitialController] behavior.
class_name TestMultiplayerEntity
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

	var mp_entity := MultiplayerEntity.new()
	mp_entity.name = "MultiplayerEntity"
	root.add_child(mp_entity)
	mp_entity.owner = root

	NetwEntity.bind(root, &"valeria", 42)

	var entity := NetwEntity.of(root)
	assert_that(root.name).is_equal("valeria|42")
	assert_that(entity.entity_id).is_equal(&"valeria")
	assert_that(entity.peer_id).is_equal(42)
	assert_that(mp_entity.entity_id).is_equal(&"valeria")
	assert_that(mp_entity.peer_id).is_equal(42)

	var rj := ResolvedJoin.new()
	rj.username = &"valeria"
	rj.peer_id = 42
	var source := {
		"spawn_index": 7,
	}

	var data := NetwEntity.decorate_spawn(source, rj)
	var netw: Dictionary = data["_netw"]
	var spawn_identity := NetwEntity.spawn_identity(data)

	assert_that(NetwEntity._spawn_identity_error(data)).is_empty()
	assert_that(NetwEntity._is_spawn_envelope(NetwEntity._spawn_envelope(rj))) \
			.is_true()
	assert_that(NetwEntity._is_spawn_envelope({ })).is_false()
	assert_that(
		NetwEntity._is_spawn_envelope(
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
	var envelope := NetwEntity._spawn_envelope(rj, payload)
	var received: Array[Variant] = []
	var wrapped := NetwEntity.wrap_spawn(
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


func test_spawn_replication_config_contract() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var entity := NetwEntity.ensure(root)
	assert_that(entity.is_template).is_false()

	var mp_entity := MultiplayerEntity.new()
	root.add_child(mp_entity)
	mp_entity.owner = root
	entity.multiplayer_entity = mp_entity
	assert_that(entity.is_template).is_true()

	var path := NodePath(":position")
	mp_entity.add_spawn_property(path)

	var cfg := mp_entity.replication_config
	assert_that(cfg.has_property(path)).is_true()
	assert_that(cfg.property_get_spawn(path)).is_true()
	assert_that(cfg.property_get_sync(path)).is_false()
	assert_that(cfg.property_get_watch(path)).is_false()
	assert_that(
		cfg.property_get_replication_mode(path),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)

	var picked := SceneReplicationConfig.new()
	var visible_path := NodePath(":visible")
	picked.add_property(visible_path)
	picked.property_set_replication_mode(
		visible_path,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	picked.property_set_spawn(visible_path, false)
	picked.property_set_sync(visible_path, true)
	picked.property_set_watch(visible_path, true)
	mp_entity.replication_config = picked

	mp_entity._sanitize_replication_config()

	assert_that(picked.property_get_spawn(visible_path)).is_true()
	assert_that(picked.property_get_sync(visible_path)).is_false()
	assert_that(picked.property_get_watch(visible_path)).is_false()
	assert_that(
		picked.property_get_replication_mode(visible_path),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)

	var expected := NodePath("MultiplayerEntity:entity_id")
	assert_that(not picked.has_property(expected)).is_true()


func test_controller_lifecycle_flow() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var entity: MultiplayerEntity = parts[1]

	entity.initial_controller = \
	MultiplayerEntity.InitialController.REPRESENTED_PEER
	entity._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(42)
	assert_that(entity.controller).is_equal(42)
	assert_that(NetwEntity.of(root).controller).is_equal(42)
	assert_that(NetwEntity.of(root).control_kind) \
			.is_equal(NetwEntity.ControlKind.PEER_CONTROLLED)

	parts = _make_player_root(42)
	root = parts[0]
	entity = parts[1]
	entity.initial_controller = MultiplayerEntity.InitialController.SERVER
	entity._on_owner_tree_entered()
	assert_that(root.get_multiplayer_authority()).is_equal(1)

	var no_peer_root: Node2D = auto_free(Node2D.new())
	no_peer_root.name = "NoSeparator"
	var no_peer_spawner := MultiplayerEntity.new()
	no_peer_spawner.name = "MultiplayerEntity"
	no_peer_root.add_child(no_peer_spawner)
	no_peer_spawner.owner = no_peer_root
	no_peer_spawner.root_path = no_peer_spawner.get_path_to(no_peer_root)
	no_peer_spawner.initial_controller = \
	MultiplayerEntity.InitialController.REPRESENTED_PEER
	no_peer_spawner._on_owner_tree_entered()
	assert_that(no_peer_root.get_multiplayer_authority()).is_equal(1)

	parts = _make_player_root(0)
	root = parts[0]
	entity = parts[1]
	entity._on_owner_tree_entered()
	entity.grant_control(42)

	var record := NetwEntity.of(root)
	assert_that(root.get_multiplayer_authority()).is_equal(42)
	assert_that(record.controller).is_equal(42)
	assert_that(record.control_kind) \
			.is_equal(NetwEntity.ControlKind.PEER_CONTROLLED)

	entity.revoke_control()

	assert_that(root.get_multiplayer_authority()).is_equal(1)
	assert_that(record.controller).is_equal(0)
	assert_that(record.control_kind) \
			.is_equal(NetwEntity.ControlKind.SERVER_CONTROLLED)

	var validator := TopologyValidator.new()
	entity.grant_control(42)
	assert_that(validator._get_expected_authority(root, entity)).is_equal(42)
	entity.revoke_control()
	assert_that(validator._get_expected_authority(root, entity)).is_equal(1)


func test_entity_lookup_and_resolution_flow() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var spawner := MultiplayerEntity.new()
	spawner.name = "MultiplayerEntity"
	root.add_child(spawner)
	spawner.owner = root

	assert_that(MultiplayerEntity.unwrap(root)).is_equal(spawner)
	assert_that(MultiplayerEntity.unwrap(auto_free(Node2D.new()))).is_null()

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


func test_multiplayer_entity_identity_forwarding() -> void:
	var mp_entity := MultiplayerEntity.new()
	auto_free(mp_entity)

	mp_entity.entity_id = &"custom_id"
	mp_entity.peer_id = 99
	assert_that(mp_entity.entity_id).is_equal(&"custom_id")
	assert_that(mp_entity.peer_id).is_equal(99)

	var root := Node2D.new()
	auto_free(root)
	root.add_child(mp_entity)
	mp_entity.owner = root

	mp_entity._notification(Node.NOTIFICATION_PARENTED)

	var entity := NetwEntity.of(root)
	assert_that(entity).is_not_null()
	assert_that(entity.entity_id).is_equal(&"custom_id")
	assert_that(entity.peer_id).is_equal(99)

	entity.entity_id = &"updated_id"
	entity.peer_id = 100
	assert_that(mp_entity.entity_id).is_equal(&"updated_id")
	assert_that(mp_entity.peer_id).is_equal(100)

	mp_entity.entity_id = &"final_id"
	mp_entity.peer_id = 101
	assert_that(entity.entity_id).is_equal(&"final_id")
	assert_that(entity.peer_id).is_equal(101)


func test_nested_entity_record_forwarding_on_tree_enter() -> void:
	var parent := Node2D.new()
	var child := Node2D.new()
	child.name = "ChildNode"
	auto_free(parent)
	auto_free(child)

	var child_entity := NetwEntity.ensure(child)
	child_entity.contribute_spawn_property(child, &"ammo")

	assert_that(child_entity._pending_spawn_props.size()).is_equal(1)

	var parent_entity := NetwEntity.ensure(parent)
	parent.add_child(child)
	child.owner = parent

	child_entity._handle_tree_entered()

	assert_that(child_entity._pending_spawn_props.is_empty()).is_true()
	assert_that(parent_entity._pending_spawn_props.size()).is_equal(1)

	var forwarded := parent_entity._pending_spawn_props[0]
	assert_that(forwarded.source).is_equal(child)
	assert_that(forwarded.property).is_equal(&"ammo")


func _make_player_root(peer_id: int) -> Array:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria|%d" % peer_id

	var entity := MultiplayerEntity.new()
	entity.name = "MultiplayerEntity"
	root.add_child(entity)
	entity.owner = root
	entity.root_path = entity.get_path_to(root)

	return [root, entity]


func test_envelope_route_and_me_interplay() -> void:
	var mt := MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	var liveness := mt.get_service(LivenessService) as LivenessService

	# 1. Reserve a route on the server
	var route := liveness.reserve_route()
	assert_that(route).is_greater(0)

	# 2. Build spawn envelope
	var rj := ResolvedJoin.new()
	rj.username = &"player1"
	rj.peer_id = 42
	var envelope := NetwEntity._spawn_envelope(rj, null, route)

	# 3. Simulate wrap_spawn callback
	var spawn_identity := NetwEntity.spawn_identity(envelope)

	var root := Node2D.new()
	root.name = "player1"
	auto_free(root)

	var me := MultiplayerEntity.new()
	me.name = "MultiplayerEntity"
	root.add_child(me)
	me.owner = root

	# Bind identity and the reserved route BEFORE entering the tree
	spawn_identity.bind(root)

	# Add to the tree to trigger _on_owner_tree_entered
	mt.add_child(root)

	# 4. Verify that route is bound correctly
	var entity := NetwEntity.of(root)
	assert_that(liveness.route_of(entity)).is_equal(route)
	assert_that(liveness.entity_of(route)).is_equal(entity)

	# 5. Let MultiplayerEntity initialize and verify it adopts the envelope's route
	assert_that(me._netw_route).is_equal(route)

