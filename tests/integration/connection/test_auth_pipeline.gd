## Integration tests for the [NetwAuthProvider] and [NetwIdentityBucket] flow.
class_name TestAuthPipeline
extends NetwTestSuite

var player_builder: PlayerBuilder
var level_builder: LevelBuilder
var spawner_path: String

var harness: NetwTestHarness
var server: MultiplayerTree
var client_tree: MultiplayerTree


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D).with_spawner()
	player_builder.pack()

	var template_instance: Node = player_builder.packed.instantiate()
	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template_instance)
	level_builder.pack()
	template_instance.free()

	spawner_path = "%s/SpawnerComponent" % player_builder.player_name

	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	server = harness.server()
	client_tree = await harness.create_connect_player_tree("AuthClient")


func test_prepare_failure_aborts_connect() -> void:
	var auth := _FailingPrepareAuth.new()
	client_tree.auth_provider = auth

	var target := JoinTarget.new()
	target.backend = client_tree.backend
	target.address = client_tree.backend.get_join_address()

	var err := await client_tree.join_or_host(
		target,
		_join_payload("alice"),
	)
	assert_that(err).is_not_equal(OK)
	assert_that(client_tree.is_online()).is_false()


func test_listen_server_host_gets_identity() -> void:
	var tree := await harness.add_listen_server(
		_join_payload("host"),
		DummyAuthProvider.new(),
	)

	var bucket := tree.get_peer_context(1).get_bucket(NetwIdentityBucket)
	assert_that(bucket.identity).is_not_null()
	assert_that(bucket.identity.username).is_equal(StringName("host"))
	assert_that(bucket.identity.service).is_equal(&"dummy")


func test_no_auth_provider_trusts_client_username() -> void:
	server.auth_provider = null
	client_tree.auth_provider = null

	var joined_rjs: Array[ResolvedJoin] = []
	server.player_joined.connect(func(rj): joined_rjs.append(rj))
	monitor_signals(server, false)
	var target := JoinTarget.new()
	target.backend = client_tree.backend
	target.address = client_tree.backend.get_join_address()

	var err := await client_tree.join_or_host(
		target,
		_join_payload("bob"),
	)
	assert_that(err).is_equal(OK)
	@warning_ignore("redundant_await")
	await assert_signal(server) \
			.wait_until(1000) \
			.is_emitted("player_joined", [any()])

	assert_that(joined_rjs).has_size(1)
	assert_that(joined_rjs[0].username).is_equal(StringName("bob"))


func _join_payload(username: String) -> JoinPayload:
	return harness.make_spawn_payload(
		username,
		level_builder.resource_path,
		spawner_path,
	)


## Auth provider whose prepare always fails.
class _FailingPrepareAuth:
	extends NetwAuthProvider

	func prepare(_payload: JoinPayload) -> Error:
		return ERR_UNAUTHORIZED
