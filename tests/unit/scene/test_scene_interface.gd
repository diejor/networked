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
	_activate(api, scene)

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
	_activate(api, scene)

	api._scenes.refresh_current_scene()

	assert_object(api._scenes.current_scene).is_same(scene)


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


func _marked_script() -> GDScript:
	var scr := GDScript.new()
	scr.source_code = "extends Node"
	scr.reload()
	return scr


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


func test_scene_mark_config_knobs_are_fluent() -> void:
	var config := NetwScriptModel.SceneMarkConfig.new()
	assert_bool(config.is_gated).is_false()
	assert_bool(config.is_session_wide).is_false()

	var returned := config.gated().session_wide().timeout(4.0)
	assert_object(returned).is_same(config)
	assert_bool(config.is_gated).is_true()
	assert_bool(config.is_session_wide).is_true()
	assert_float(config.deadline).is_equal(4.0)


func test_a_packed_destination_answers_the_stem_the_book_is_keyed_by() -> void:
	var api := _api()
	var packed: PackedScene = load(
		"res://tests/support/scene/marked_test_scene.tscn",
	)
	var stem := StringName(packed.get_state().get_node_name(0))
	var scene: Node = auto_free(_scene(stem))
	_activate(api, scene)

	assert_str(String(stem)).is_not_equal(
		packed.resource_path.get_file().get_basename(),
	)
	assert_object(api._scenes._existing_destination(packed)).is_same(scene)
	assert_bool(api._scenes._packed_scene_active(packed)).is_true()


# Registers [param scene] as live under its level's stem, which is what a
# spawned container's tree entry does.
func _activate(api: NetwMultiplayer, scene: Node) -> void:
	api._scenes.core.scene_enter(
		api.entity_of(scene),
		StringName(scene.get_child(0).name),
		false,
	)


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
