## Integration tests for the transition-driven session lifecycle.
##
## Covers a same-tree host -> leave -> host cycle, the server-crash convergence
## path, and a disconnect-then-join-a-different-backend workflow. Each asserts
## that [signal MultiplayerTree.session_entered] and
## [signal MultiplayerTree.session_ended] tear session state down so the second
## session starts from a clean slate.
class_name TestSessionRelifecycle
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayerFull/MultiplayerEntity"

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
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)


func _listen_payload(username: String) -> JoinPayload:
	return harness.make_spawn_payload(
		username,
		level_builder.resource_path,
		SPAWNER_PATH,
	)


func _gate_count(tree: MultiplayerTree) -> int:
	return tree.find_children("*", "InterestGate", true, false).size()


func _host_listen_with_spawned_player(username: String) -> MultiplayerTree:
	var tree := await harness.add_listen_server(_listen_payload(username))
	await harness.wait_for_player(tree, level_builder.scene_name)
	return tree


func _record_session_order(tree: MultiplayerTree) -> Array[String]:
	var order: Array[String] = []
	tree.session_entered.connect(func() -> void: order.append("entered"))
	tree.session_ended.connect(func() -> void: order.append("ended"))
	return order


func _assert_session_teardown_empty(tree: MultiplayerTree) -> void:
	var sm := tree.get_service(MultiplayerSceneManager)
	var interest := tree.get_service(InterestService)
	assert_int(tree.state).is_equal(MultiplayerTree.State.OFFLINE)
	assert_int(tree.role).is_equal(MultiplayerTree.Role.NONE)
	assert_bool(sm.active_scenes.is_empty()).is_true()
	assert_bool(interest.all_layers().is_empty()).is_true()
	assert_int(_gate_count(tree)).is_equal(0)


func _rehost_with_spawned_player(
		tree: MultiplayerTree,
		username: String,
) -> Error:
	var err: Error = await tree.host_player(_listen_payload(username))
	if err == OK:
		await harness.wait_for_player(tree, level_builder.scene_name)
	return err


func _join_shared_backend_without_spawn(
		tree: MultiplayerTree,
		username: String,
) -> Error:
	await harness.host_server()
	var backend := LocalLoopbackBackend.new()
	backend.session = harness.session()
	var target := JoinTarget.new()
	target.backend = backend
	target.address = backend.get_join_address()

	return await tree.join(target, harness.make_sceneless_payload(username))


func test_rehost_on_same_tree_rebuilds_session_from_empty() -> void:
	var tree := await _host_listen_with_spawned_player("alice")
	var sm := tree.get_service(MultiplayerSceneManager)
	assert_bool(sm.active_scenes.is_empty()).is_false()
	var first_gates := _gate_count(tree)
	assert_int(first_gates).is_greater(0)

	var order := _record_session_order(tree)

	await tree.disconnect_player()
	_assert_session_teardown_empty(tree)

	var err := await _rehost_with_spawned_player(tree, "alice")
	assert_int(err).is_equal(OK)

	assert_int(tree.state).is_equal(MultiplayerTree.State.ONLINE)
	assert_int(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)
	assert_bool(sm.active_scenes.is_empty()).is_false()
	assert_int(_gate_count(tree)).is_equal(first_gates)
	assert_array(order).is_equal(["ended", "entered"])


func test_server_crash_converges_to_offline_and_no_role() -> void:
	var client := await harness.add_client()
	assert_int(client.state).is_equal(MultiplayerTree.State.ONLINE)
	assert_int(client.role).is_equal(MultiplayerTree.Role.CLIENT)

	var ended := [0]
	client.session_ended.connect(func() -> void: ended[0] += 1)

	# Simulate the api dropping the server out from under a live client.
	client._on_server_disconnected()

	assert_int(client.state).is_equal(MultiplayerTree.State.OFFLINE)
	assert_int(client.role).is_equal(MultiplayerTree.Role.NONE)
	assert_int(ended[0]).is_equal(1)

	# A second crash signal while already offline is a no-op, never a re-tear.
	client._on_server_disconnected()
	assert_int(ended[0]).is_equal(1)


func test_disconnect_then_join_different_backend_on_same_tree() -> void:
	var tree := await _host_listen_with_spawned_player("alice")

	await tree.disconnect_player()
	_assert_session_teardown_empty(tree)

	var err := await _join_shared_backend_without_spawn(tree, "alice")
	assert_int(err).is_equal(OK)

	assert_int(tree.state).is_equal(MultiplayerTree.State.ONLINE)
	assert_int(tree.role).is_equal(MultiplayerTree.Role.CLIENT)
	assert_int(_gate_count(tree)).is_equal(0)
