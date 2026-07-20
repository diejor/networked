## Integration tests for [MultiplayerSceneManager] scene lifecycle API.
class_name TestLobbyLifecycle
extends NetwTestSuite

var harness: NetwTestHarness
var server_scenes: NetwSceneInterface
var level_builder: LevelBuilder
var level_2_builder: LevelBuilder


func before_test() -> void:
	level_builder = LevelBuilder.new("TestLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	level_2_builder = LevelBuilder.new("TestLevel2") \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_2_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	server_scenes = harness.server().api.scenes
	harness.register_spawnable_scene(level_builder.packed)
	harness.register_spawnable_scene(level_2_builder.packed)
	await harness.add_client()


func test_scene_load_policy_flow() -> void:
	assert_that(server_scenes.scene(level_builder.scene_name) != null).is_true()
	assert_that(server_scenes.scene(level_2_builder.scene_name) != null).is_true()

	server_scenes.spawn_scene(level_builder.scene_name)
	assert_that(server_scenes.scenes.size()).is_equal(2)

	server_scenes.freeze(level_builder.scene_name)
	assert_that(server_scenes.scene(level_builder.scene_name) != null).is_true()

	var h2 := make_unmanaged_harness()
	await h2.setup_factory(NetwTestSuite.create_scene_manager)
	h2.register_spawnable_scene(level_builder.packed)
	h2.register_spawnable_scene(level_2_builder.packed, false)
	await h2.add_client()

	var mgr2_scenes := h2.server().api.scenes
	assert_that(mgr2_scenes.scene(level_builder.scene_name) != null).is_true()
	assert_that(mgr2_scenes.scene(level_2_builder.scene_name) != null).is_false()
	await h2.teardown()


func test_scene_activation_cache_and_removal_flow() -> void:
	server_scenes.destroy(level_2_builder.scene_name)
	await get_tree().process_frame

	var path := level_2_builder.resource_path
	server_scenes.preload_scene(level_2_builder.scene_name)

	assert_that(path).is_not_empty()
	assert_that(
		server_scenes.is_scene_preloaded(level_2_builder.scene_name),
	).is_true()
	assert_that(
		server_scenes.scene(level_2_builder.scene_name) != null,
	).is_false()

	server_scenes.spawn_scene(level_2_builder.scene_name)

	assert_that(
		server_scenes.is_scene_preloaded(level_2_builder.scene_name),
	).is_false()
	assert_that(server_scenes.scene(level_2_builder.scene_name) != null).is_true()

	server_scenes.destroy(level_2_builder.scene_name)
	await get_tree().process_frame

	@warning_ignore("redundant_await")
	await server_scenes.activate_scene(level_2_builder.scene_name)
	assert_that(server_scenes.scene(level_2_builder.scene_name) != null).is_true()

	var scene := server_scenes.scene(level_builder.scene_name)
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED

	@warning_ignore("redundant_await")
	await server_scenes.activate_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	server_scenes.freeze(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)

	@warning_ignore("redundant_await")
	await server_scenes.activate_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	server_scenes.destroy(level_builder.scene_name)
	await get_tree().process_frame

	assert_that(server_scenes.scene(level_builder.scene_name) != null).is_false()
	assert_that(is_instance_valid(scene)).is_false()

	server_scenes.spawn_scene(level_builder.scene_name)
	assert_that(server_scenes.scene(level_builder.scene_name) != null).is_true()

	scene = server_scenes.scene(level_builder.scene_name)
	server_scenes.retire(level_builder.scene_name, 2)

	assert_that(server_scenes.scene(level_builder.scene_name) != null).is_false()
	assert_that(is_instance_valid(scene)).is_true()

	await drain_frames(get_tree(), 3)

	assert_that(is_instance_valid(scene)).is_false()


func test_api_scene_lifecycle_verbs_and_signals() -> void:
	var scenes := harness.server().api.scenes
	var activated: Array[MultiplayerScene] = []
	var despawned: Array[MultiplayerScene] = []
	scenes.scene_activated.connect(activated.append)
	scenes.scene_despawned.connect(despawned.append)
	var scene := scenes.scene(level_builder.scene_name)

	scenes.freeze(level_builder.scene_name)
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)

	assert_object(scenes.activate(level_builder.scene_name)).is_same(scene)
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)
	assert_array(activated).contains([scene])

	scenes.destroy(level_builder.scene_name)
	await get_tree().process_frame

	assert_object(scenes.scene(level_builder.scene_name)).is_null()
	assert_int(despawned.size()).is_equal(1)


func test_scene_emptied_reports_the_last_peer_leaving() -> void:
	@warning_ignore("redundant_await")
	await server_scenes.activate_scene(level_builder.scene_name)
	var scene := server_scenes.scene(level_builder.scene_name)
	var emptied: Array[MultiplayerScene] = []
	harness.server().api.scenes.scene_emptied.connect(emptied.append)
	var player := _join_player()
	player.queue_free()
	await drain_frames(get_tree(), 3)

	assert_int(emptied.size()).is_equal(1)
	assert_object(emptied[0]).is_same(scene)
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_api_move_reparents_entity_and_updates_participant_scene() -> void:
	var api := harness.server().api
	var source := api.scenes.scene(level_builder.scene_name)
	var destination := api.scenes.scene(level_2_builder.scene_name)
	var participant := api.participants[0]
	var player := _add_scene_player(
		source,
		participant.peer_id,
		participant.username,
	)
	var entity := NetwEntity.of(player)
	var moved: Array[NetwEntity] = []
	api.scenes.entity_moved.connect(
		func(value: NetwEntity, _from, _to): moved.append(value),
	)

	var promise := api.scenes.move(entity, destination)
	if not promise.is_completed:
		await promise.completed

	assert_int(promise.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(player.get_parent()).is_same(destination.level)
	assert_object(participant.current_scene).is_same(destination)
	assert_array(moved).contains([entity])


func test_single_change_to_moves_session_and_destroys_source() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		func() -> MultiplayerSceneManager:
			var manager := NetwTestSuite.create_scene_manager()
			manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
			return manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	var api := h.server().api
	var participant := api.participant(
		client.multiplayer_peer.get_unique_id(),
	)

	var promise := api.scenes.change_to(level_2_builder.scene_name)
	if not promise.is_completed:
		await promise.completed
	await drain_frames(get_tree(), 2)

	var destination := api.scenes.scene(level_2_builder.scene_name)
	assert_int(promise.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(destination).is_not_null()
	assert_object(api.scenes.scene(level_builder.scene_name)).is_null()
	assert_object(participant.current_scene).is_same(destination)
	await h.teardown()


func test_scene_change_requests_deny_supersede_and_allow() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		func() -> MultiplayerSceneManager:
			var manager := NetwTestSuite.create_scene_manager()
			manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
			return manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	# A declared scene admits by default. A listener vetoes any request that does
	# not carry the expected arg.
	h.server().api.scenes.change_requested.connect(
		func(rq: SceneChangeRequest) -> void:
			if rq.args != [&"allow"]:
				rq.deny(),
	)
	var first := client.api.scenes.request_change(level_2_builder.scene_name)
	var denied := client.api.scenes.request_change(level_2_builder.scene_name)
	await _wait_scene_promise(denied)

	assert_int(first.result).is_equal(NetwScenePromise.Result.SUPERSEDED)
	assert_int(denied.result).is_equal(NetwScenePromise.Result.DENIED)
	var allowed := client.api.scenes.request_change(
		level_2_builder.scene_name,
		[&"allow"],
	)
	await _wait_scene_promise(allowed)

	assert_int(allowed.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(
		h.server().api.scenes.scene(level_2_builder.scene_name),
	).is_not_null()
	await h.teardown()


func test_scene_change_path_request_denies_then_allows() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		func() -> MultiplayerSceneManager:
			var manager := NetwTestSuite.create_scene_manager()
			manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
			return manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	var path := level_2_builder.resource_path
	# A raw path to an unmarked scene is deny-default, since nothing bounds it.
	var denied := client.api.scenes.request_change_path(path)
	await _wait_scene_promise(denied)

	assert_int(denied.result).is_equal(NetwScenePromise.Result.DENIED)
	# A listener may still allow it from trusted server code.
	h.server().api.scenes.change_requested.connect(
		func(rq: SceneChangeRequest) -> void:
			rq.allow(),
	)
	var allowed := client.api.scenes.request_change_path(path)
	await _wait_scene_promise(allowed)

	assert_int(allowed.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(
		h.server().api.scenes.scene(level_2_builder.scene_name),
	).is_not_null()
	await h.teardown()


func test_marked_path_request_admitted_without_a_policy() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		func() -> MultiplayerSceneManager:
			var manager := NetwTestSuite.create_scene_manager()
			manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
			return manager,
	)
	var marked := load(
			"res://tests/support/scene/marked_test_scene.tscn",
	) as PackedScene
	h.register_spawnable_scene(marked, false)
	var client := await h.add_client()
	# The mark is the path allowlist, so a marked non-gated scene admits the
	# request with no server-side code at all.
	var allowed := client.api.scenes.request_change_path(marked.resource_path)
	await _wait_scene_promise(allowed)
	assert_int(allowed.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(h.server().api.scenes.scene(&"MarkedTestScene")).is_not_null()

	# A listener can still veto a marked scene.
	h.server().api.scenes.change_requested.connect(
		func(rq: SceneChangeRequest) -> void:
			rq.deny(),
	)
	var vetoed := client.api.scenes.request_change_path(marked.resource_path)
	await _wait_scene_promise(vetoed)
	assert_int(vetoed.result).is_equal(NetwScenePromise.Result.DENIED)
	await h.teardown()


func test_front_door_routes_authority_and_client() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		func() -> MultiplayerSceneManager:
			var manager := NetwTestSuite.create_scene_manager()
			manager.concurrency = NetwSceneConfig.Concurrency.SINGLE
			return manager,
	)
	var marked := load(
			"res://tests/support/scene/marked_test_scene.tscn",
	) as PackedScene
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	h.register_spawnable_scene(marked, false)
	var client := await h.add_client()

	# Server authority applies the change directly, no request round trip.
	var server := h.server()
	var direct := server.api.scenes.change_scene_to_file(
		server.api.root, level_2_builder.resource_path,
	)
	await _wait_scene_promise(direct)
	assert_int(direct.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(server.api.scenes.scene(level_2_builder.scene_name)) \
			.is_not_null()

	# A client's front door becomes a request; the marked scene admits it with
	# no server policy installed.
	var requested := client.api.scenes.change_scene_to_file(
		client.api.root, marked.resource_path,
	)
	await _wait_scene_promise(requested)
	assert_int(requested.result).is_equal(NetwScenePromise.Result.OK)
	assert_object(server.api.scenes.scene(&"MarkedTestScene")).is_not_null()
	await h.teardown()


func _wait_scene_promise(promise: NetwScenePromise) -> void:
	for i in 180:
		if promise.is_completed:
			return
		await get_tree().process_frame
	assert_bool(promise.is_completed).is_true()


func _join_player() -> Node:
	var scene := (
			server_scenes.scene(level_builder.scene_name) as MultiplayerScene
	)
	return _add_scene_player(scene, 1001, &"test_player")


func _add_scene_player(
		scene: MultiplayerScene,
		peer_id: int,
		username: StringName,
) -> Node:
	var player := Node2D.new()
	NetwEntity.bind(player, username, peer_id)
	scene.add_player(NetwEntity.of(player))
	return player
