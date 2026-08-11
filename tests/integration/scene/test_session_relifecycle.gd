## Integration tests for the transition-driven session lifecycle.
##
## Covers a same-tree host -> leave -> host cycle, the server-crash convergence
## path, and a disconnect-then-join-a-different-backend workflow. Each asserts
## that [signal MultiplayerTree.session_entered] and
## [signal MultiplayerTree.session_ended] tear session state down so the second
## session starts from a clean slate.
class_name TestSessionRelifecycle
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayerFull"

var harness: NetwTestHarness
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("TestPlayerFull") \
			.with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)


func _listen_payload(username: String) -> JoinPayload:
	return harness.make_spawn_payload(
		username,
		level_builder.resource_path,
		SPAWNER_PATH,
	)


func _host_listen_with_spawned_player(username: String) -> MultiplayerTree:
	var tree := await harness.add_listen_server(_listen_payload(username))
	await harness.wait_for_player(tree, level_builder.scene_name)
	return tree


func _record_session_order(tree: MultiplayerTree) -> Array[String]:
	var order: Array[String] = []
	tree.api.session_entered.connect(func() -> void: order.append("entered"))
	tree.api.session_ended.connect(func() -> void: order.append("ended"))
	return order


func _assert_session_teardown_empty(tree: MultiplayerTree) -> void:
	var api := tree.api
	var interest := tree.api._interest
	assert_int(tree.state).is_equal(SessionCore.State.OFFLINE)
	assert_int(tree.role).is_equal(SessionCore.Role.NONE)
	assert_bool(api.scene_instances().is_empty()).is_true()
	assert_bool(interest.all_layers().is_empty()).is_true()


func _rehost_with_spawned_player(
		tree: MultiplayerTree,
		username: String,
) -> Error:
	var err: Error = await tree.host(_listen_payload(username))
	if err == OK:
		await harness.wait_for_player(tree, level_builder.scene_name)
	return err


func _join_shared_backend_without_spawn(
		tree: MultiplayerTree,
		username: String,
) -> Error:
	await harness.host_server()
	tree.transport = NetwLocalParams.new()
	LocalLoopbackSession.set_shared_session(harness.session())
	var target := NetwConnectTarget.new()
	target.scheme = &"local"
	target.address = "localhost"

	return NetwConnector.error_of(
		await NetwConnector.of(tree.api).join(
			target,
			harness.make_sceneless_payload(username),
		),
	)


func test_rehost_on_same_tree_rebuilds_session_from_empty() -> void:
	var tree := await _host_listen_with_spawned_player("valeria")
	var api := tree.api
	assert_bool(api.scene_instances().is_empty()).is_false()
	var first_scene_count := api.scene_instances().size()
	assert_int(first_scene_count).is_greater(0)

	var order := _record_session_order(tree)

	await tree.api.session.leave()
	_assert_session_teardown_empty(tree)

	var err := await _rehost_with_spawned_player(tree, "valeria")
	assert_int(err).is_equal(OK)

	assert_int(tree.state).is_equal(SessionCore.State.ONLINE)
	assert_int(tree.role).is_equal(SessionCore.Role.LISTEN_SERVER)
	assert_bool(api.scene_instances().is_empty()).is_false()
	assert_int(api.scene_instances().size()).is_equal(first_scene_count)
	assert_array(order).is_equal(["ended", "entered"])


func test_server_crash_converges_to_offline_and_no_role() -> void:
	var client := await harness.add_client()
	assert_int(client.state).is_equal(SessionCore.State.ONLINE)
	assert_int(client.role).is_equal(SessionCore.Role.CLIENT)

	var ended := [0]
	client.api.session_ended.connect(func() -> void: ended[0] += 1)

	# Simulate the api dropping the server out from under a live client. The
	# session machine on the api reacts to this edge, not the tree.
	client.api.server_disconnected.emit()

	assert_int(client.state).is_equal(SessionCore.State.OFFLINE)
	assert_int(client.role).is_equal(SessionCore.Role.NONE)
	assert_int(ended[0]).is_equal(1)

	# A second crash signal while already offline is a no-op, never a re-tear.
	client.api.server_disconnected.emit()
	assert_int(ended[0]).is_equal(1)


func test_disconnect_then_join_different_backend_on_same_tree() -> void:
	var tree := await _host_listen_with_spawned_player("valeria")

	await tree.api.session.leave()
	_assert_session_teardown_empty(tree)

	var err := await _join_shared_backend_without_spawn(tree, "valeria")
	assert_int(err).is_equal(OK)

	assert_int(tree.state).is_equal(SessionCore.State.ONLINE)
	assert_int(tree.role).is_equal(SessionCore.Role.CLIENT)
	assert_bool(tree.api.scene_instances().is_empty()).is_true()
