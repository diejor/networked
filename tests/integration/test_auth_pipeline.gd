## Integration tests for the [NetwAuthProvider] and [NetwIdentityBucket] flow.
class_name TestAuthPipeline
extends NetwTestSuite

const TEST_LEVEL_SCENE := preload(
	"res://addons/networked_test/fixtures/TestLevel.tscn"
)
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var harness: NetwTestHarness
var server: MultiplayerTree
var client_tree: MultiplayerTree


func before_test() -> void:
	harness = make_harness()
	await harness.setup(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(TEST_LEVEL_SCENE)
	server = harness.server()
	client_tree = await harness.create_connect_player_tree("AuthClient")


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	super.after_test()


func test_prepare_failure_aborts_connect() -> void:
	var auth := _FailingPrepareAuth.new()
	client_tree.auth_provider = auth

	var err := await client_tree.connect_player(_join_payload("alice"))
	assert_that(err).is_not_equal(OK)
	assert_that(client_tree.is_online()).is_false()


func test_listen_server_host_gets_identity() -> void:
	var tree := await harness.add_listen_server(
		_join_payload("host"),
		DummyAuthProvider.new()
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
	var err := await client_tree.connect_player(_join_payload("bob"))
	assert_that(err).is_equal(OK)
	await wait_until(func(): return joined_rjs.size() == 1)

	assert_that(joined_rjs).has_size(1)
	assert_that(joined_rjs[0].username).is_equal(StringName("bob"))


func _join_payload(username: String) -> JoinPayload:
	return harness.make_join_payload(
		username,
		TEST_LEVEL_SCENE.resource_path,
		SPAWNER_PATH
	)


## Auth provider whose _prepare always fails.
class _FailingPrepareAuth:
	extends NetwAuthProvider

	func _prepare(_payload: JoinPayload) -> Error:
		return ERR_UNAUTHORIZED
