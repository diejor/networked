## Integration tests for [method MultiplayerTree.auto_connect_player] and
## [method MultiplayerTree.host_player].
class_name TestMultiplayerTreeConnect
extends NetwTestSuite

## Path from the level root to the player spawn template.
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


func test_client_is_online_after_auto_connect_player() -> void:
	var tree := await harness.add_connect_player(
		harness.make_sceneless_payload("valeria"),
	)

	assert_that(tree.api.is_online).is_true()


func test_host_starts_server_and_joins() -> void:
	var tree := await harness.add_host(
		harness.make_sceneless_payload("valeria"),
	)

	assert_that(tree.api.is_online).is_true()

	var server_node := harness.get_node_or_null("Server")
	assert_that(server_node).is_not_null()
	assert_that(server_node).is_instanceof(MultiplayerTree)
	var server_tree := server_node as MultiplayerTree

	# Interest is no longer a mounted service node. The interface is owned by
	# the tree's api and always present.
	assert_that(server_tree.api._interest).is_not_null()
	assert_that(server_tree.api._interest).is_instanceof(InterestCore)


func test_listen_server_auto_connect_player_spawns_player() -> void:
	var tree := await harness.add_listen_server(
		harness.make_spawn_payload(
			"valeria",
			level_builder.resource_path,
			SPAWNER_PATH,
		),
	)

	assert_that(tree.role).is_equal(SessionCore.Role.LISTEN_SERVER)

	var player := await harness.wait_for_player(tree, level_builder.scene_name)
	assert_that(player).is_not_null()
	assert_that(player.name).is_equal("valeria|1")


func test_join_fail_fast_on_offline_address() -> void:
	var tree := MultiplayerTree.new()
	add_child(tree)

	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = "127.0.0.1"

	var payload := JoinPayload.new()
	payload.username = "offline_client"

	tree.api.state_changed.connect(
		func(_old: SessionCore.State, new: SessionCore.State) -> void:
			if new != SessionCore.State.CONNECTING:
				return
			var trigger_failure: Callable
			trigger_failure = func() -> void:
				var api := tree.api
				if (
						api != null
						and not api.connection_failed
						.get_connections().is_empty()
				):
					api.connection_failed.emit()
				else:
					trigger_failure.call_deferred()
			trigger_failure.call_deferred()
	)

	var time_before := Time.get_ticks_msec()
	var result := await NetwConnector.of(tree.api).join(target, payload, true)
	var elapsed := (Time.get_ticks_msec() - time_before) / 1000.0

	assert_int(NetwConnector.error_of(result)).is_equal(ERR_CANT_CONNECT)
	assert_bool(elapsed < 4.0).is_true()
	# The verb's own result is the outcome, so a caller never reads a
	# last-attempt mirror to learn what happened.
	assert_that(result).is_not_null()
	assert_str(result.message).is_equal("Could not reach the server.")

	tree.queue_free()
