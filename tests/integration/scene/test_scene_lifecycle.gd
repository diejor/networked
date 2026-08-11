## Integration tests for [MultiplayerSceneManager] scene lifecycle API.
class_name TestLobbyLifecycle
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var server_core: SceneCore
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
	server_api = harness.server().api
	server_core = server_api._scenes
	harness.register_spawnable_scene(level_builder.packed)
	harness.register_spawnable_scene(level_2_builder.packed)
	await harness.add_client()


func test_scene_load_policy_flow() -> void:
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()
	assert_that(server_api.scene(level_2_builder.scene_name) != null).is_true()

	server_core.spawn_scene(level_builder.scene_name)
	assert_that(server_api.scene_instances().size()).is_equal(2)

	server_core.freeze(level_builder.scene_name)
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()

	var h2 := make_unmanaged_harness()
	await h2.setup_factory(NetwTestSuite.create_scene_manager)
	h2.register_spawnable_scene(level_builder.packed)
	h2.register_spawnable_scene(level_2_builder.packed, false)
	await h2.add_client()

	var mgr2_api := h2.server().api
	assert_that(mgr2_api.scene(level_builder.scene_name) != null).is_true()
	assert_that(mgr2_api.scene(level_2_builder.scene_name) != null).is_false()
	await h2.teardown()


func test_scene_activation_cache_and_removal_flow() -> void:
	server_core.destroy(level_2_builder.scene_name)
	await get_tree().process_frame

	var path := level_2_builder.resource_path
	assert_that(path).is_not_empty()
	assert_that(
		server_api.scene(level_2_builder.scene_name) != null,
	).is_false()

	server_core.spawn_scene(level_2_builder.scene_name)

	assert_that(server_api.scene(level_2_builder.scene_name) != null).is_true()

	server_core.destroy(level_2_builder.scene_name)
	await get_tree().process_frame

	@warning_ignore("redundant_await")
	await server_core.activate_scene(level_2_builder.scene_name)
	assert_that(server_api.scene(level_2_builder.scene_name) != null).is_true()

	var scene := server_api.scene(level_builder.scene_name)
	var container := scene.level_container()
	scene.level.process_mode = Node.PROCESS_MODE_DISABLED

	@warning_ignore("redundant_await")
	await server_core.activate_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	server_core.freeze(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)

	@warning_ignore("redundant_await")
	await server_core.activate_scene(level_builder.scene_name)
	assert_that(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)

	server_core.destroy(level_builder.scene_name)
	await get_tree().process_frame

	assert_that(server_api.scene(level_builder.scene_name) != null).is_false()
	assert_that(is_instance_valid(container)).is_false()

	server_core.spawn_scene(level_builder.scene_name)
	assert_that(server_api.scene(level_builder.scene_name) != null).is_true()

	scene = server_api.scene(level_builder.scene_name)
	var retired := scene.level_container()
	server_core.retire(level_builder.scene_name, 2)

	assert_that(server_api.scene(level_builder.scene_name) != null).is_false()
	assert_that(is_instance_valid(retired)).is_true()

	await drain_frames(get_tree(), 3)

	assert_that(is_instance_valid(retired)).is_false()


func test_api_scene_lifecycle_verbs_and_signals() -> void:
	var core := harness.server().api._scenes
	var activated: Array[Node] = []
	var despawned: Array[Node] = []
	core.scene_activated.connect(activated.append)
	core.scene_despawned.connect(despawned.append)
	var scene := core.scene(level_builder.scene_name)

	core.freeze(level_builder.scene_name)
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_DISABLED)

	assert_object(core.activate(level_builder.scene_name)) \
			.is_same(scene.level_container())
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)
	assert_array(activated).contains([scene.level_container()])

	core.destroy(level_builder.scene_name)
	await get_tree().process_frame

	assert_object(core.scene(level_builder.scene_name)).is_null()
	assert_int(despawned.size()).is_equal(1)


func test_a_departing_player_releases_its_participant() -> void:
	@warning_ignore("redundant_await")
	await server_core.activate_scene(level_builder.scene_name)
	var scene := server_api.scene(level_builder.scene_name)
	# A real participant, because the scene reports departures in participant
	# terms and a synthetic peer id has no roster row to report.
	var participant := server_api.participants[0]
	var left: Array[NetwParticipant] = []
	scene.on_participant_left(left.append)
	var player := _add_scene_player(
		scene,
		participant.peer_id,
		participant.username,
	)
	player.queue_free()
	# The release settles over idle hops, so drain rather than pin a hop count.
	await drain_frames(get_tree(), 6)

	assert_int(left.size()).is_equal(1)
	assert_bool(scene.admits(participant.peer_id)).is_false()
	assert_int(scene.level.process_mode).is_equal(Node.PROCESS_MODE_INHERIT)


func test_api_move_reparents_entity_and_updates_participant_scene() -> void:
	var api := harness.server().api
	var source := api.scene(level_builder.scene_name)
	var destination := api.scene(level_2_builder.scene_name)
	var participant := api.participants[0]
	var player := _add_scene_player(
		source,
		participant.peer_id,
		participant.username,
	)
	var entity := NetwEntity.of(player)
	var moved: Array[NetwEntity] = []
	entity.reparented.connect(func(_opts): moved.append(entity))

	var promise := destination.move_in(entity)
	if not promise.is_settled:
		await promise.settled

	assert_int(promise.code).is_equal(OK)
	assert_object(player.get_parent()).is_same(destination.level)
	assert_object(participant.current_scene).is_same(destination)
	assert_array(moved).contains([entity])


func test_single_change_to_moves_session_and_destroys_source() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		NetwTestSuite.create_scene_manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	var api := h.server().api
	var participant := api.peer_get_participant(
		client.multiplayer_peer.get_unique_id(),
	)

	# Session reach is what makes authority's own front door replace the world.
	api.scene_set_request_reach(NetwMultiplayer.SceneReach.SCENE_REACH_SESSION)
	var promise := Netw.change_scene_to_file(
		api.root,
		level_2_builder.resource_path,
	)
	if not promise.is_settled:
		await promise.settled
	await drain_frames(get_tree(), 2)

	var destination := api.scene(level_2_builder.scene_name)
	assert_int(promise.code).is_equal(OK)
	assert_object(destination).is_not_null()
	assert_object(api.scene(level_builder.scene_name)).is_null()
	assert_object(participant.current_scene).is_same(destination)
	await h.teardown()


func test_request_reach_decides_who_a_request_moves() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var mover := await h.add_client()
	var stayer := await h.add_client()
	var api := h.server().api
	var moved := api.peer_get_participant(
		mover.multiplayer_peer.get_unique_id(),
	)
	var other := api.peer_get_participant(
		stayer.multiplayer_peer.get_unique_id(),
	)
	var source := api.scene(level_builder.scene_name)
	source.admit(moved)
	source.admit(other)
	await drain_frames(get_tree(), 2)

	# The default reach answers the request the requester actually made.
	assert_int(api.scene_get_request_reach()).is_equal(
		NetwMultiplayer.SceneReach.SCENE_REACH_PARTICIPANT,
	)
	var alone := mover.api.scene_request(level_2_builder.scene_name)
	if not alone.is_settled:
		await alone.settled
	await drain_frames(get_tree(), 2)

	var destination := api.scene(level_2_builder.scene_name)
	assert_int(alone.code).is_equal(OK)
	assert_object(moved.current_scene).is_same(destination)
	assert_object(other.current_scene).is_same(source)

	# Session reach carries everyone, which is change_to for every request.
	api.scene_set_request_reach(
		NetwMultiplayer.SceneReach.SCENE_REACH_SESSION,
	)
	var together := stayer.api.scene_request(level_builder.scene_name)
	if not together.is_settled:
		await together.settled
	await drain_frames(get_tree(), 2)

	# The claim is that everyone ends up together, not which container instance
	# won the replacement.
	assert_int(together.code).is_equal(OK)
	assert_object(moved.current_scene).is_same(other.current_scene)
	assert_that(other.current_scene.label).is_equal(level_builder.scene_name)
	await h.teardown()


func test_scene_change_requests_deny_supersede_and_allow() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		NetwTestSuite.create_scene_manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	# A declared scene admits by default. A listener vetoes any request that does
	# not carry the expected arg.
	h.server().api.scene_set_request_handler(
		func(_p: NetwParticipant, _dest: Variant, args: Array) -> Error:
			return OK if args == [&"allow"] else ERR_UNAUTHORIZED,
	)
	var first := client.api.scene_request(level_2_builder.scene_name)
	var denied := client.api.scene_request(level_2_builder.scene_name)
	await _wait_scene_promise(denied)

	assert_int(first.code).is_equal(ERR_SKIP)
	assert_int(denied.code).is_equal(ERR_UNAUTHORIZED)
	var allowed := client.api.scene_request(
		level_2_builder.scene_name,
		[&"allow"],
	)
	await _wait_scene_promise(allowed)

	assert_int(allowed.code).is_equal(OK)
	assert_object(
		h.server().api.scene(level_2_builder.scene_name),
	).is_not_null()
	await h.teardown()


func test_scene_change_path_request_denies_then_allows() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		NetwTestSuite.create_scene_manager,
	)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	var path := level_2_builder.resource_path
	# A raw path to an unmarked scene is deny-default, since nothing bounds it.
	var denied := client.api.scene_request(path)
	await _wait_scene_promise(denied)

	assert_int(denied.code).is_equal(ERR_UNAUTHORIZED)
	# A listener may still allow it from trusted server code.
	h.server().api.scene_set_request_handler(
		func(_p: NetwParticipant, _dest: Variant, _args: Array) -> Error:
			return OK,
	)
	var allowed := client.api.scene_request(path)
	await _wait_scene_promise(allowed)

	assert_int(allowed.code).is_equal(OK)
	assert_object(
		h.server().api.scene(level_2_builder.scene_name),
	).is_not_null()
	await h.teardown()


func test_a_handler_matches_a_destination_in_either_form() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(level_builder.packed)
	h.register_spawnable_scene(level_2_builder.packed, false)
	var client := await h.add_client()
	var api := h.server().api
	# The same handler, written against the stem alone, must answer the same
	# way for the path form. Comparing the raw destination would not.
	api.scene_set_request_handler(
		func(_p: NetwParticipant, destination: Variant, _args: Array) -> Error:
			if api.scene_request_targets(destination, level_2_builder.scene_name):
				return OK
			return ERR_UNAUTHORIZED,
	)

	var by_stem := client.api.scene_request(level_2_builder.scene_name)
	await _wait_scene_promise(by_stem)
	var by_path := client.api.scene_request(level_2_builder.resource_path)
	await _wait_scene_promise(by_path)
	var elsewhere := client.api.scene_request(level_builder.scene_name)
	await _wait_scene_promise(elsewhere)

	assert_int(by_stem.code).is_equal(OK)
	assert_int(by_path.code).is_equal(OK)
	assert_int(elsewhere.code).is_equal(ERR_UNAUTHORIZED)
	await h.teardown()


func test_marked_path_request_admitted_without_a_policy() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		NetwTestSuite.create_scene_manager,
	)
	var marked := load(
		"res://tests/support/scene/marked_test_scene.tscn",
	) as PackedScene
	h.register_spawnable_scene(marked, false)
	var client := await h.add_client()
	# The mark is the path allowlist, so a marked non-gated scene admits the
	# request with no server-side code at all.
	var allowed := client.api.scene_request(marked.resource_path)
	await _wait_scene_promise(allowed)
	assert_int(allowed.code).is_equal(OK)
	assert_object(h.server().api.scene(&"MarkedTestScene")).is_not_null()

	# A listener can still veto a marked scene.
	h.server().api.scene_set_request_handler(
		func(_p: NetwParticipant, _dest: Variant, _args: Array) -> Error:
			return ERR_UNAUTHORIZED,
	)
	var vetoed := client.api.scene_request(marked.resource_path)
	await _wait_scene_promise(vetoed)
	assert_int(vetoed.code).is_equal(ERR_UNAUTHORIZED)
	await h.teardown()


func test_front_door_routes_authority_and_client() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(
		NetwTestSuite.create_scene_manager,
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
	var direct := Netw.change_scene_to_file(
		server.api.root,
		level_2_builder.resource_path,
	)
	await _wait_scene_promise(direct)
	assert_int(direct.code).is_equal(OK)
	assert_object(server.api.scene(level_2_builder.scene_name)) \
			.is_not_null()

	# A client's front door becomes a request; the marked scene admits it with
	# no server policy installed.
	var requested := Netw.change_scene_to_file(
		client.api.root,
		marked.resource_path,
	)
	await _wait_scene_promise(requested)
	assert_int(requested.code).is_equal(OK)
	assert_object(server.api.scene(&"MarkedTestScene")).is_not_null()
	await h.teardown()


func _wait_scene_promise(promise: NetwPromise) -> void:
	for i in 180:
		if promise.is_settled:
			return
		await get_tree().process_frame
	assert_bool(promise.is_settled).is_true()


func _add_scene_player(
		scene: NetwSceneHandle,
		peer_id: int,
		username: StringName,
) -> Node:
	var player := Node2D.new()
	NetwEntity.bind(player, username, peer_id)
	scene.add_player(NetwEntity.of(player))
	return player
