class_name TestSpawnPropertyTransport
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var player_scene: PackedScene
var level_builder: LevelBuilder


func before_test() -> void:
	player_scene = _make_probe_player_scene()
	level_builder = LevelBuilder.new("SpawnPropertyTransportLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_scene])
	level_builder.pack()

	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.add_spawnable_scene(level_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)
	client0 = await harness.add_client()
	client1 = await harness.add_client()


func test_self_property_decodes_before_spawn_lifecycle() -> void:
	var server_player := _spawn_probe_player(client0, "ordering")
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
		harness.player_name_for(client0),
	)
	var client_probe := _probe(client_player)

	assert_that(_probe(server_player).identity_packet["marker"]) \
			.is_equal("ordering")
	assert_that(client_probe.identity_packet["marker"]).is_equal("ordering")
	assert_that(client_probe.has_marker_at(&"parented_after_super", "ordering")) \
			.is_false()
	assert_that(client_probe.has_marker_at(&"owner_tree_entered", "ordering")) \
			.is_false()
	assert_that(
		client_probe.has_marker_at(&"enter_tree_before_super", "ordering"),
	) \
			.is_true()
	assert_that(client_probe.has_marker_at(&"ready_before_super", "ordering")) \
			.is_true()


func test_spawn_value_capture_uses_pre_add_child_configure_value() -> void:
	var server_player := _spawn_probe_player(client0, "configured")
	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
		harness.player_name_for(client0),
	)

	assert_that(_probe(server_player).identity_packet["marker"]) \
			.is_equal("configured")
	assert_that(_probe(client_player).identity_packet["marker"]) \
			.is_equal("configured")


func test_late_joiner_receives_current_server_value() -> void:
	var server_player := _spawn_probe_player(client0, "initial")
	await harness.wait_for_player(
		client0,
		level_builder.scene_name,
		harness.player_name_for(client0),
	)
	_probe(server_player).identity_packet = _packet("late")

	var late_client := await harness.add_client()
	_spawn_probe_player(late_client, "late_own")
	var late_player := await harness.wait_for_player(
		late_client,
		level_builder.scene_name,
		harness.player_name_for(client0),
	)

	assert_that(_probe(late_player).identity_packet["marker"]).is_equal("late")


func test_template_state_stays_unbound_without_identity_packet() -> void:
	var template := player_scene.instantiate()
	auto_free(template)
	var probe := _probe(template)

	assert_that(probe.identity_packet.is_empty()).is_true()
	assert_that(probe.is_template).is_true()


func test_identity_packet_can_drive_record_without_encoded_name() -> void:
	var peer_id := client0.multiplayer_peer.get_unique_id()
	var template := player_scene.instantiate()
	var player := MultiplayerEntity.instantiate_from(
		template,
		func(entity: MultiplayerEntity) -> void:
			var probe := entity as SpawnIdentityProbeEntity
			probe.owner.name = "CosmeticPlayer"
			probe.identity_packet = _identity_packet(&"packet_player", peer_id)
			probe.entity_id = &"packet_player"
			probe.peer_id = peer_id
	)
	template.free()
	var scene := harness.scene_on_server(level_builder.scene_name)
	scene.add_player(player)

	var client_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
		&"CosmeticPlayer",
	)
	var client_probe := _probe(client_player)

	assert_that(player.name).is_equal("CosmeticPlayer")
	assert_that(client_player.name).is_equal("CosmeticPlayer")
	assert_that(client_probe.identity_packet["entity_id"]) \
			.is_equal(&"packet_player")
	assert_that(client_probe.identity_packet["peer_id"]).is_equal(peer_id)


func _spawn_probe_player(client: MultiplayerTree, marker: String) -> Node:
	var peer_id := client.multiplayer_peer.get_unique_id()
	var username: String = client.get_meta(&"_harness_username")
	var template := player_scene.instantiate()
	var player := MultiplayerEntity.instantiate_from(
		template,
		func(entity: MultiplayerEntity) -> void:
			var probe := entity as SpawnIdentityProbeEntity
			probe.identity_packet = _identity_packet(StringName(username), peer_id)
			probe.identity_packet["marker"] = marker
	)
	template.free()
	NetwEntity.bind(player, StringName(username), peer_id)
	var scene := harness.scene_on_server(level_builder.scene_name)
	scene.add_player(player)
	return player


func _make_probe_player_scene() -> PackedScene:
	var root := Node2D.new()
	root.name = "SpawnIdentityProbePlayer"

	var probe := SpawnIdentityProbeEntity.new()
	probe.initial_controller = MultiplayerEntity.InitialController.REPRESENTED_PEER
	probe.set_meta("_custom_type_script", "uid://spawnidentityprobe")
	SceneAssembly.attach(root, probe, root)

	var sync := MultiplayerSynchronizer.new()
	sync.name = "PlayerSync"
	sync.replication_config = SceneReplicationConfig.new()
	SceneAssembly.attach(root, sync, root)
	sync.root_path = sync.get_path_to(root)

	var path := NetwPathNamespace.next_path(
		"player",
		"SpawnIdentityProbePlayer",
	)
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed


func _identity_packet(entity_id: StringName, peer_id: int) -> Dictionary:
	var packet := _packet("identity")
	packet["entity_id"] = entity_id
	packet["peer_id"] = peer_id
	return packet


func _packet(marker: String) -> Dictionary:
	return {
		"marker": marker,
	}


func _probe(node: Node) -> SpawnIdentityProbeEntity:
	return MultiplayerEntity.unwrap(node) as SpawnIdentityProbeEntity
