## Unit coverage for [SceneCore] and the positional lookups it answers.
class_name TestSceneInterface
extends NetwTestSuite

var _apis: Array[NetwMultiplayer] = []


# A bare NetwMultiplayer holds a reference cycle with its session, so it never
# frees on refcount alone. Disposing it after each test breaks that cycle.
func after_test() -> void:
	for api in _apis:
		if is_instance_valid(api) and not api._disposing:
			api.embedding.dispose()
	_apis.clear()
	await super.after_test()


func _api() -> NetwMultiplayer:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	_apis.append(api)
	return api


func test_interface_exists_without_a_tree() -> void:
	var api := _api()

	assert_object(api._scenes).is_not_null()
	assert_object(api.get_service(MultiplayerSceneManager)).is_null()


func test_scene_config_registration_is_api_owned() -> void:
	var api := _api()
	var source: Node = auto_free(Node.new())
	var config := NetwSceneConfig.new()
	config.isolation = NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD

	assert_bool(api._scenes.has_declaration()).is_false()
	api.object_configuration_add(source, config)
	assert_bool(api._scenes.has_declaration()).is_true()
	assert_int(api._scenes._default_isolation()).is_equal(NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD)

	api.object_configuration_remove(source, config)
	assert_bool(api._scenes.has_declaration()).is_false()


func test_active_registry_answers_scene_lookups() -> void:
	var api := _api()
	var scene: Node = auto_free(_scene(&"Arena"))
	api._scenes.scenes[&"Arena"] = scene

	assert_object(api._scenes.scene(&"Arena").level_container()).is_same(scene)
	assert_object(api._scenes.scene(&"Missing")).is_null()


func test_scene_lookup_and_entity_scene_share_one_answer() -> void:
	var scene: Node = auto_free(_scene(&"Arena"))
	var entity_root := Node.new()
	var child := Node.new()
	scene.get_child(0).add_child(entity_root)
	entity_root.add_child(child)
	var entity := NetwEntity.ensure(entity_root)

	assert_object(NetwEntity.of(child).scene.level_container()).is_same(scene)
	assert_object(entity.scene.level_container()).is_same(scene)


func test_single_scene_drives_current_scene() -> void:
	var api := _api()
	var scene: Node = auto_free(_scene(&"Arena"))
	api._scenes.scenes[&"Arena"] = scene

	api._scenes.refresh_current_scene()

	assert_object(api._scenes.current_scene).is_same(scene)


func test_a_second_shared_world_scene_is_allowed() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)
	var scene: Node = auto_free(_scene(&"First"))
	scenes.scenes[&"First"] = scene

	# Several shared-world scenes are legal now that isolation is per scene, so
	# a second one is allowed rather than refused.
	assert_bool(scenes._can_spawn_scene(&"Second")).is_true()


func test_a_non_isolated_scene_builds_a_plain_wrapper() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)

	var scene: Variant = scenes._make_scene_wrapper(
		true,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_NONE,
	)

	assert_bool(scene.get_script() == null).is_true()
	assert_bool(scene is SubViewport).is_false()
	assert_int(scene.get_child_count()).is_equal(0)
	scene.free()


func test_an_isolated_scene_builds_a_world_owning_wrapper_on_the_host() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)

	var scene: Variant = scenes._make_scene_wrapper(
		true,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	)

	assert_bool(scene.get_script() == null).is_true()
	assert_bool(scene is SubViewport).is_true()
	assert_bool((scene as SubViewport).own_world_3d).is_true()
	assert_that((scene as SubViewport).render_target_update_mode).is_equal(
		SubViewport.UPDATE_DISABLED,
	)
	scene.free()


func test_an_isolated_scene_builds_a_plain_wrapper_on_a_viewer() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)

	var scene: Variant = scenes._make_scene_wrapper(
		false,
		NetwMultiplayer.SceneIsolation.SCENE_ISOLATION_OWN_WORLD,
	)

	assert_bool(scene.get_script() == null).is_true()
	assert_bool(scene is SubViewport).is_false()
	scene.free()


func test_native_scene_change_tripwire_names_networked_verbs() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)
	api._session.state = SessionCore.State.ONLINE
	var native_scene: Node = auto_free(Node.new())
	var expected := (
			"Native change_scene_to_* to an unmarked scene during an online "
			+ "session. The replicated session is intact, but this client left the "
			+ "presented game locally. Mark the scene with "
			+ "Netw.configure_multiplayer_scene() to make the change a server "
			+ "request, or change scenes with Netw.change_scene_to_file(), which "
			+ "applies on authority and asks from a client."
	)

	await assert_error(
		func() -> void:
			scenes._on_native_scene_changed(native_scene)
	).is_push_error(expected)
	api.embedding.dispose()


func test_native_scene_change_tripwire_silent_when_offline() -> void:
	# With no online session the native pointer is plain local UI (a menu or
	# boot scene), so the tripwire stays quiet rather than teaching a fix.
	var api := _api()
	var scenes := SceneCore.new(api)
	var native_scene: Node = auto_free(Node.new())

	await assert_error(
		func() -> void:
			scenes._on_native_scene_changed(native_scene)
	).is_success()
	api.embedding.dispose()


func test_scene_request_deadline_resolves_timed_out() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)
	var promise := NetwPromise.new()
	scenes._pending_request = promise
	# An id is drawn rather than planted: the record plane exposes no setter for
	# the pending one, because an id a caller could assign without opening would
	# let it answer a request nobody made.
	scenes._next_request_id = 7
	scenes.core.open_request()

	scenes._on_request_deadline(7)

	assert_int(promise.code).is_equal(ERR_TIMEOUT)
	api.embedding.dispose()


func test_scene_request_deadline_ignores_a_stale_request_id() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)
	var promise := NetwPromise.new()
	scenes._pending_request = promise
	# An id is drawn rather than planted: the record plane exposes no setter for
	# the pending one, because an id a caller could assign without opening would
	# let it answer a request nobody made.
	scenes._next_request_id = 7
	scenes.core.open_request()

	scenes._on_request_deadline(6)

	assert_bool(promise.is_settled).is_false()
	api.embedding.dispose()


func test_scene_result_rejects_a_non_server_sender() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)
	var promise := NetwPromise.new()
	scenes._pending_request = promise
	# An id is drawn rather than planted: the record plane exposes no setter for
	# the pending one, because an id a caller could assign without opening would
	# let it answer a request nobody made.
	scenes._next_request_id = 7
	scenes.core.open_request()

	scenes._handle_scene_result_frame(
		var_to_bytes([7, ERR_UNAUTHORIZED]),
		2,
	)

	assert_bool(promise.is_settled).is_false()
	assert_object(scenes._pending_request).is_same(promise)
	api.embedding.dispose()


func test_mark_and_configure_register_multiplayer_scene() -> void:
	var scr := _marked_script()
	assert_bool(Netw.is_multiplayer_scene(scr)).is_false()

	Netw.mark_multiplayer_scene(scr)
	assert_bool(Netw.is_multiplayer_scene(scr)).is_true()

	var node: Node = auto_free(Node.new())
	node.set_script(scr)
	var config := Netw.configure_multiplayer_scene(node)
	config.timeout(8.0)

	assert_float(config.deadline).is_equal(8.0)
	# One config per script: another instance shares it.
	var node_2: Node = auto_free(Node.new())
	node_2.set_script(scr)
	assert_object(Netw.configure_multiplayer_scene(node_2)).is_same(config)
	assert_object(NetwScriptModel.get_scene_config(scr)).is_same(config)


func test_native_scene_change_tripwire_exempts_a_configured_scene() -> void:
	var scr := _marked_script()
	var node: Node = auto_free(Node.new())
	node.set_script(scr)
	Netw.configure_multiplayer_scene(node)
	var api := _api()
	var scenes := SceneCore.new(api)
	api._session.state = SessionCore.State.ONLINE

	await assert_error(
		func() -> void:
			scenes._on_native_scene_changed(node)
	).is_success()
	api.embedding.dispose()


func _marked_script() -> GDScript:
	var scr := GDScript.new()
	scr.source_code = "extends Node"
	scr.reload()
	return scr


func test_scene_request_flood_guard_bounds_a_peer() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)

	# The host at peer 1 carries authority and is never limited.
	for i in 20:
		assert_bool(scenes._request_flooded(1)).is_false()

	# A remote peer is allowed up to the limit, then flooded.
	for i in 8:
		assert_bool(scenes._request_flooded(42)).is_false()
	assert_bool(scenes._request_flooded(42)).is_true()
	assert_int(
		api.get_stat(NetwMultiplayer.Stat.STAT_VERDICT_BUSY),
	).is_equal(1)
	api.embedding.dispose()


func test_verify_requested_path_bounds_the_scene_file() -> void:
	var api := _api()
	var scenes := SceneCore.new(api)

	assert_str(scenes._verify_requested_path("res://a/level.tscn")) \
			.is_equal("res://a/level.tscn")
	assert_str(scenes._verify_requested_path("res://a/level.scn")) \
			.is_equal("res://a/level.scn")
	assert_str(scenes._verify_requested_path("res://a/level.gd")).is_empty()
	assert_str(scenes._verify_requested_path("user://a/level.tscn")).is_empty()
	assert_str(scenes._verify_requested_path("")).is_empty()
	api.embedding.dispose()


func test_pathless_native_entry_errors_without_requesting() -> void:
	# An in-memory change_scene_to_packed or a programmatic node has no resource
	# path, so it cannot become a path request. The hook teaches the fix and
	# sends nothing rather than desyncing.
	var api := _api()
	var scenes := SceneCore.new(api)
	var node: Node = auto_free(Node.new())
	var expected := (
			"A marked scene entered natively has no resource_path, so it "
			+ "cannot become a server request. Instantiate it from a saved "
			+ "scene file."
	)

	await assert_error(
		func() -> void:
			scenes._handle_native_scene_entry(node)
	).is_push_error(expected)
	assert_object(scenes._pending_request).is_null()
	api.embedding.dispose()


func test_pending_hook_is_a_side_effect_callback() -> void:
	# on_pending is a side-effect hook: the framework calls the method and
	# ignores its return, leaving the game to present its own loading UI.
	var api := _api()
	var scenes := SceneCore.new(api)
	var scr := GDScript.new()
	scr.source_code = (
			"extends Node\n"
			+ "var shown := false\n"
			+ "func _show_loading() -> void:\n"
			+ "\tshown = true\n"
	)
	scr.reload()
	var node: Node = auto_free(Node.new())
	node.set_script(scr)
	var config := NetwScriptModel.SceneMarkConfig.new()
	config.pending_method = &"_show_loading"

	scenes._invoke_pending_hook(node, config)
	assert_bool(node.get(&"shown")).is_true()
	api.embedding.dispose()


func test_pending_hook_absent_without_on_pending() -> void:
	# No mark method, or a missing one, is a silent no-op rather than an error.
	var api := _api()
	var scenes := SceneCore.new(api)
	var node: Node = auto_free(Node.new())
	node.set_script(_marked_script())

	await assert_error(
		func() -> void:
			scenes._invoke_pending_hook(node, NetwScriptModel.SceneMarkConfig.new())
			var missing := NetwScriptModel.SceneMarkConfig.new()
			missing.pending_method = &"_absent"
			scenes._invoke_pending_hook(node, missing)
			scenes._invoke_pending_hook(node, null)
	).is_success()
	api.embedding.dispose()


func test_scene_promise_resolves_once() -> void:
	var promise := NetwPromise.new()
	var outcomes: Array[int] = []
	promise.completed.connect(outcomes.append)

	promise.resolve(OK)
	promise.reject(ERR_UNAUTHORIZED)

	assert_bool(promise.is_settled).is_true()
	assert_int(promise.code).is_equal(OK)
	assert_array(outcomes).is_equal([OK])


func test_scene_mark_config_knobs_are_fluent() -> void:
	var config := NetwScriptModel.SceneMarkConfig.new()
	assert_bool(config.is_gated).is_false()
	assert_bool(config.is_session_wide).is_false()

	var returned := config.gated().session_wide().timeout(4.0)
	assert_object(returned).is_same(config)
	assert_bool(config.is_gated).is_true()
	assert_bool(config.is_session_wide).is_true()
	assert_float(config.deadline).is_equal(4.0)


func _scene(scene_name: StringName) -> Node:
	var scene := Node.new()
	scene.name = scene_name
	scene.set_meta(NetwSceneHandle._SCENE_META, true)
	NetwEntity.ensure(scene)
	var level := Node.new()
	level.name = scene_name
	scene.add_child(level)
	level.owner = scene
	return scene
