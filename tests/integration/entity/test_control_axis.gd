class_name TestControlAxis
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var client1: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("ControlAxisPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	level_builder = LevelBuilder.new("ControlAxisLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.add_spawnable_scene(level_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)
	client0 = await harness.add_client()
	client1 = await harness.add_client()


func test_request_control_grants_when_not_denied() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client0, harness.player_name_for(client0))
	var client_player := await _wait_for_player_on(
		client1,
		harness.player_name_for(client0),
	)

	var server_entity := _mp(server_player)
	server_entity.transfer = MultiplayerEntity.Transfer.REQUESTABLE
	var client_entity := _mp(client_player)
	var peer_id := client1.multiplayer_peer.get_unique_id()

	client_entity.request_control()

	await _wait_until(
		func() -> bool:
			return server_player.get_multiplayer_authority() == peer_id,
		"server authority to move",
	)
	await _wait_until(
		func() -> bool:
			return client_player.get_multiplayer_authority() == peer_id,
		"client authority to move",
	)

	assert_that(NetwEntity.of(server_player).controller).is_equal(peer_id)
	assert_that(NetwEntity.of(client_player).controller).is_equal(peer_id)


func test_control_request_can_be_denied() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))

	var server_entity := _mp(server_player)
	server_entity.transfer = MultiplayerEntity.Transfer.REQUESTABLE
	server_entity.control_requested.connect(
		func(_peer_id: int, request: MultiplayerEntity.ControlRequest) -> void:
			request.deny()
	)

	_mp(_client_player(client1, client0)).request_control()
	await NetwTestSuite.drain_frames(get_tree(), 5)

	var original_peer := client0.multiplayer_peer.get_unique_id()
	assert_that(server_player.get_multiplayer_authority()) \
			.is_equal(original_peer)
	assert_that(NetwEntity.of(server_player).controller).is_equal(original_peer)


func test_fixed_transfer_rejects_request() -> void:
	var server_player := harness.spawn_player(client0, player_builder.packed)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))

	_mp(_client_player(client1, client0)).request_control()
	await NetwTestSuite.drain_frames(get_tree(), 5)

	var original_peer := client0.multiplayer_peer.get_unique_id()
	assert_that(server_player.get_multiplayer_authority()) \
			.is_equal(original_peer)
	assert_that(NetwEntity.of(server_player).controller).is_equal(original_peer)


func test_controller_disconnect_reverts_to_server() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))
	var controller_peer := client1.multiplayer_peer.get_unique_id()
	var server_entity := _mp(server_player)

	server_entity.grant_control(controller_peer)
	await _wait_until(
		func() -> bool:
			return server_player.get_multiplayer_authority() == controller_peer,
		"server authority to grant",
	)

	await harness.disconnect_client(client1)
	await _wait_until(
		func() -> bool:
			return server_player.get_multiplayer_authority() == 1,
		"controller disconnect revert",
	)

	assert_that(NetwEntity.of(server_player).controller).is_equal(0)
	assert_that(is_instance_valid(server_player)).is_true()


func test_controller_disconnect_can_despawn() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))
	var controller_peer := client1.multiplayer_peer.get_unique_id()
	var server_entity := _mp(server_player)
	server_entity.on_controller_disconnect = \
	MultiplayerEntity.DisconnectRule.DESPAWN

	server_entity.grant_control(controller_peer)
	await _wait_until(
		func() -> bool:
			return server_player.get_multiplayer_authority() == controller_peer,
		"server authority to grant",
	)

	var server_player_ref: WeakRef = weakref(server_player)
	await harness.disconnect_client(client1)
	await _wait_until(
		func() -> bool:
			return _is_freed(server_player_ref),
		"controller disconnect despawn",
	)


func test_late_joiner_receives_current_controller() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))
	var controller_peer := client1.multiplayer_peer.get_unique_id()

	_mp(server_player).grant_control(controller_peer)
	await _wait_until(
		func() -> bool:
			return server_player.get_multiplayer_authority() == controller_peer,
		"server authority to grant",
	)

	var late_client := await harness.add_client()
	harness.spawn_player(late_client, player_builder.packed)
	var late_player := await _wait_for_player_on(
		late_client,
		harness.player_name_for(client0),
	)

	assert_that(NetwEntity.of(late_player).controller).is_equal(controller_peer)
	assert_that(late_player.get_multiplayer_authority()) \
			.is_equal(controller_peer)


func test_represented_peer_disconnect_despawns_before_revert() -> void:
	var server_player := _spawn_control_player(client0)
	harness.spawn_player(client1, player_builder.packed)
	await _wait_for_player_on(client1, harness.player_name_for(client0))
	var represented_peer := client0.multiplayer_peer.get_unique_id()
	var server_entity := _mp(server_player)
	server_entity.on_controller_disconnect = \
	MultiplayerEntity.DisconnectRule.REVERT_TO_SERVER

	server_entity.grant_control(represented_peer)
	var server_player_ref: WeakRef = weakref(server_player)
	await harness.disconnect_client(client0)

	await _wait_until(
		func() -> bool:
			return _is_freed(server_player_ref),
		"represented peer despawn",
	)


func _spawn_control_player(client: MultiplayerTree) -> Node:
	var player := harness.spawn_player(client, player_builder.packed)
	_mp(player).transfer = MultiplayerEntity.Transfer.REQUESTABLE
	return player


func _client_player(
		viewer: MultiplayerTree,
		represented: MultiplayerTree,
) -> Node:
	var scene := harness.scene_manager_for(viewer) \
			.active_scenes[level_builder.scene_name] as MultiplayerScene
	return _find_player(scene, harness.player_name_for(represented))


func _wait_for_player_on(
		viewer: MultiplayerTree,
		player_name: StringName,
) -> Node:
	var player := await harness.wait_for_player(
		viewer,
		level_builder.scene_name,
		player_name,
	)
	assert_that(player).is_not_null()
	return player


func _find_player(scene: MultiplayerScene, player_name: StringName) -> Node:
	for player in scene.player_nodes():
		if player.name == player_name:
			return player
	return null


func _mp(node: Node) -> MultiplayerEntity:
	return MultiplayerEntity.unwrap(node)


func _is_freed(ref: WeakRef) -> bool:
	return not is_instance_valid(ref.get_ref())


func _wait_until(cond: Callable, label: String) -> void:
	var deadline := Time.get_ticks_msec() + 1000
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return
		await get_tree().process_frame
	assert_that(cond.call()).override_failure_message(label).is_true()
