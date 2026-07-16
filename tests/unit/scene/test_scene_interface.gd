## Unit coverage for the API-owned scene registry and positional lookups.
class_name TestSceneInterface
extends NetwTestSuite

var _apis: Array[NetwMultiplayer] = []


# A bare NetwMultiplayer holds a reference cycle with its session, so it never
# frees on refcount alone. Disposing it after each test breaks that cycle.
func after_test() -> void:
	for api in _apis:
		if is_instance_valid(api) and not api._disposing:
			api.dispose()
	_apis.clear()
	await super.after_test()


func _api() -> NetwMultiplayer:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	_apis.append(api)
	return api


func test_interface_exists_without_a_tree() -> void:
	var api := _api()

	assert_object(api.scenes).is_not_null()
	assert_object(api.scene_manager).is_null()


func test_manager_registration_feeds_the_active_registry() -> void:
	var api := _api()
	var manager: MultiplayerSceneManager = auto_free(MultiplayerSceneManager.new())
	var config := NetwSceneConfig.new()
	var scene: MultiplayerScene = auto_free(_scene(&"Arena"))
	manager.active_scenes[&"Arena"] = scene

	api.object_configuration_add(manager, config)

	assert_object(api.scene_manager).is_same(manager)
	assert_object(api.scenes.scene(&"Arena")).is_same(scene)

	api.object_configuration_remove(manager, config)
	assert_object(api.scene_manager).is_null()
	assert_object(api.scenes.scene(&"Arena")).is_null()


func test_scene_lookup_and_entity_scene_share_one_answer() -> void:
	var scene: MultiplayerScene = auto_free(_scene(&"Arena"))
	var entity_root := Node.new()
	var child := Node.new()
	scene.level.add_child(entity_root)
	entity_root.add_child(child)
	var entity := NetwEntity.ensure(entity_root)

	assert_object(MultiplayerScene.of(child)).is_same(scene)
	assert_object(entity.scene).is_same(scene)


func test_single_scene_drives_current_scene() -> void:
	var api := _api()
	var manager: MultiplayerSceneManager = auto_free(MultiplayerSceneManager.new())
	var config := NetwSceneConfig.new()
	var scene: MultiplayerScene = auto_free(_scene(&"Arena"))
	manager.active_scenes[&"Arena"] = scene

	api.object_configuration_add(manager, config)
	api.scenes.refresh_current_scene()

	assert_object(api.scenes.current_scene).is_same(scene)


func test_native_scene_change_tripwire_names_networked_verbs() -> void:
	var api := _api()
	api.session.state = NetwSessionInterface.State.ONLINE
	var native_scene: Node = auto_free(Node.new())
	var expected := (
			"Native change_scene_to_* to an unmarked scene during an online "
			+ "session. The replicated session is intact, but this client left the "
			+ "presented game locally. Mark the scene with "
			+ "Netw.configure_multiplayer_scene() to make the change a server "
			+ "request, or use api.scenes.request_change() on a client or "
			+ "api.scenes.change_to() on the server."
	)

	await assert_error(
		func() -> void:
			api.scenes._on_native_scene_changed(native_scene)
	).is_push_error(expected)
	api.dispose()


func test_scene_request_deadline_resolves_timed_out() -> void:
	var api := _api()
	var promise := NetwScenePromise.new()
	api.scenes._pending_request = promise
	api.scenes._pending_request_id = 7

	api.scenes._on_request_deadline(7)

	assert_int(promise.result).is_equal(NetwScenePromise.Result.TIMED_OUT)
	api.dispose()


func test_scene_request_deadline_ignores_a_stale_request_id() -> void:
	var api := _api()
	var promise := NetwScenePromise.new()
	api.scenes._pending_request = promise
	api.scenes._pending_request_id = 7

	api.scenes._on_request_deadline(6)

	assert_bool(promise.is_completed).is_false()
	api.dispose()


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
	api.session.state = NetwSessionInterface.State.ONLINE

	await assert_error(
		func() -> void:
			api.scenes._on_native_scene_changed(node)
	).is_success()
	api.dispose()


func _marked_script() -> GDScript:
	var scr := GDScript.new()
	scr.source_code = "extends Node"
	scr.reload()
	return scr


func test_scene_request_flood_guard_bounds_a_peer() -> void:
	var api := _api()
	var scenes := api.scenes

	# The host at peer 1 carries authority and is never limited.
	for i in 20:
		assert_bool(scenes._request_flooded(1)).is_false()

	# A remote peer is allowed up to the limit, then flooded.
	for i in 8:
		assert_bool(scenes._request_flooded(42)).is_false()
	assert_bool(scenes._request_flooded(42)).is_true()
	api.dispose()


func test_verify_requested_path_bounds_the_scene_file() -> void:
	var api := _api()
	var scenes := api.scenes

	assert_str(scenes._verify_requested_path("res://a/level.tscn")) \
			.is_equal("res://a/level.tscn")
	assert_str(scenes._verify_requested_path("res://a/level.scn")) \
			.is_equal("res://a/level.scn")
	assert_str(scenes._verify_requested_path("res://a/level.gd")).is_empty()
	assert_str(scenes._verify_requested_path("user://a/level.tscn")).is_empty()
	assert_str(scenes._verify_requested_path("")).is_empty()
	api.dispose()


func test_scene_promise_resolves_once() -> void:
	var promise := NetwScenePromise.new()
	var outcomes: Array[int] = []
	promise.completed.connect(outcomes.append)

	promise.resolve(NetwScenePromise.Result.OK)
	promise.resolve(NetwScenePromise.Result.DENIED)

	assert_bool(promise.is_completed).is_true()
	assert_int(promise.result).is_equal(NetwScenePromise.Result.OK)
	assert_array(outcomes).is_equal([NetwScenePromise.Result.OK])


func _scene(scene_name: StringName) -> MultiplayerScene:
	var scene := MultiplayerScene.new()
	var level := Node.new()
	level.name = scene_name
	scene.level = level
	return scene
