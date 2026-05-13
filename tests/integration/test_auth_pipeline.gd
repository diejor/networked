## Integration tests for the [NetwAuthProvider] and [NetwIdentityBucket] flow.
class_name TestAuthPipeline
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const SPAWNER_PATH := "TestPlayerFull/SpawnerComponent"

var session: LocalLoopbackSession
var server: MultiplayerTree
var client_tree: MultiplayerTree


func before_test() -> void:
	session = LocalLoopbackSession.new()
	_setup_server()
	_setup_client()
	await get_tree().process_frame
	server.host()


func after_test() -> void:
	session = null


func test_prepare_failure_aborts_connect() -> void:
	var auth := _FailingPrepareAuth.new()
	client_tree.auth_provider = auth

	var err := await client_tree.connect_player(_create_join_payload("alice"))
	assert_that(err).is_not_equal(OK)
	assert_that(client_tree.is_online()).is_false()


func test_listen_server_host_gets_identity() -> void:
	var tree := MultiplayerTree.new()
	tree.name = "ListenServer"
	tree.use_listen_server = true
	tree.auto_host_headless = false
	tree.auth_provider = DummyAuthProvider.new()
	add_child(tree)
	auto_free(tree)

	var ls_session := LocalLoopbackSession.new()
	var backend := LocalLoopbackBackend.new()
	backend.session = ls_session
	tree.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	tree.add_child(mgr)
	mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	var err := await tree.connect_player(_create_join_payload("host"))
	assert_that(err).is_equal(OK)

	var bucket := tree.get_peer_context(1).get_bucket(NetwIdentityBucket)
	assert_that(bucket.identity).is_not_null()
	assert_that(bucket.identity.username).is_equal(StringName("host"))
	assert_that(bucket.identity.service).is_equal(&"dummy")


func test_no_auth_provider_trusts_client_username() -> void:
	server.auth_provider = null
	client_tree.auth_provider = null

	var joined_rjs: Array[ResolvedJoin] = []
	server.player_joined.connect(func(rj): joined_rjs.append(rj))
	client_tree.connect_player(_create_join_payload("bob"))
	await timeout_await(client_tree.connected_to_server)
	for _i in range(10):
		await get_tree().process_frame

	assert_that(joined_rjs).has_size(1)
	assert_that(joined_rjs[0].username).is_equal(StringName("bob"))


func _setup_server() -> void:
	server = MultiplayerTree.new()
	server.name = "Server"
	server.is_server = true
	server.auto_host_headless = false
	add_child(server)
	auto_free(server)

	var backend := LocalLoopbackBackend.new()
	backend.session = session
	server.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	server.add_child(mgr)
	mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)


func _setup_client() -> void:
	client_tree = MultiplayerTree.new()
	client_tree.name = "ClientTree"
	add_child(client_tree)
	auto_free(client_tree)

	var backend := LocalLoopbackBackend.new()
	backend.session = session
	client_tree.backend = backend

	var mgr: MultiplayerSceneManager = NetworkedTestSuite.create_scene_manager()
	client_tree.add_child(mgr)


func _create_join_payload(username: String) -> JoinPayload:
	var spawner_component_path := SceneNodePath.new()
	spawner_component_path.scene_path = TEST_LEVEL_SCENE.resource_path
	spawner_component_path.node_path = SPAWNER_PATH

	var data := JoinPayload.new()
	data.username = username
	data.url = "localhost"
	data.spawner_component_path = spawner_component_path
	return data


## Auth provider whose _prepare always fails.
class _FailingPrepareAuth:
	extends NetwAuthProvider

	func _prepare(_payload: JoinPayload) -> Error:
		return ERR_UNAUTHORIZED
