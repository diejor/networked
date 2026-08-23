## Conformance coverage for the tier-1 native-change capture matrix.
##
## Each test asserts one (role, mode) cell of the capture decision table: a
## native [method SceneTree.change_scene_to_file] or
## [method SceneTree.change_scene_to_packed] during a live session resolves to
## the same [NetwMultiplayer] verb the tier-2 door would call. Both funnel
## through one instance-entry hook, so the tests drive it with an instantiated
## [PackedScene], the shape both native calls produce. The capture gate
## ([method Netw.configure_multiplayer_scene]) stays off in a multiplexed
## process, so the decision-table method runs on the
## server and client interfaces directly, the way the mark hook would once
## exactly one session owns the presentation.
class_name TestSceneCaptureMatrix
extends NetwTestSuite

const MARKED_SCENE_PATH := "res://tests/support/scene/marked_test_scene.tscn"
const MARKED_SCENE_NAME := &"MarkedTestScene"


func _single_manager() -> Callable:
	return func() -> MultiplayerSceneManager:
		var manager := NetwTestSuite.create_scene_manager()
		return manager


func _level(level_name: String) -> LevelBuilder:
	var builder := LevelBuilder.new(level_name).with_root(Node2D)
	builder.pack()
	return builder


func _marked_scene() -> PackedScene:
	return load(MARKED_SCENE_PATH) as PackedScene


# A native instance carries its source file path, the byte a native
# change_scene_to_file hands the capture hook.
func _native_instance(packed: PackedScene) -> Node:
	return packed.instantiate()


func _wait_scene_promise(promise: NetwPromise) -> void:
	for i in 180:
		if promise.is_settled:
			return
		await get_tree().process_frame
	assert_bool(promise.is_settled).is_true()

# --- Server cells -----------------------------------------------------------


func test_server_single_active_captures_change_to() -> void:
	var source := _level("CaptureSource")
	var dest := _level("CaptureDest")
	var h := make_unmanaged_harness()
	await h.setup_factory(_single_manager())
	h.register_spawnable_scene(source.packed)
	await h.add_client()
	var scenes := h.server().api
	assert_object(scenes.scene(source.scene_name)).is_not_null()

	# A native change while a SINGLE scene is active replaces the whole session.
	scenes.scene_set_request_reach(
		NetwMultiplayer.SceneReach.SCENE_REACH_SESSION,
	)
	scenes._scene_handle_native_entry(_native_instance(dest.packed))
	await drain_frames(get_tree(), 3)

	assert_object(scenes.scene(dest.scene_name)).is_not_null()
	assert_object(scenes.scene(source.scene_name)).is_null()
	await h.teardown()


func test_server_single_idle_captures_activate() -> void:
	var dest := _level("CaptureIdleDest")
	var h := make_unmanaged_harness()
	await h.setup_factory(_single_manager())
	# No initial scene: the server presents nothing until the native change.
	h.register_spawnable_scene(dest.packed, false)
	await h.add_client()
	var scenes := h.server().api
	assert_object(scenes.scene(dest.scene_name)).is_null()

	# With no active scene the native change activates the first one.
	scenes._scene_handle_native_entry(_native_instance(dest.packed))
	await drain_frames(get_tree(), 3)

	assert_object(scenes.scene(dest.scene_name)).is_not_null()
	await h.teardown()


func test_server_concurrent_captures_activate() -> void:
	var source := _level("CaptureConcSource")
	var dest := _level("CaptureConcDest")
	var h := make_unmanaged_harness()
	# The default manager is CONCURRENT.
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(source.packed)
	await h.add_client()
	var scenes := h.server().api

	# A native change under CONCURRENT activates a second world; the first stays.
	scenes._scene_handle_native_entry(_native_instance(dest.packed))
	await drain_frames(get_tree(), 3)

	assert_object(scenes.scene(dest.scene_name)).is_not_null()
	assert_object(scenes.scene(source.scene_name)).is_not_null()
	await h.teardown()

# --- Client cells -----------------------------------------------------------


func test_client_single_captures_request_and_admits_marked() -> void:
	var h := make_unmanaged_harness()
	await h.setup_factory(_single_manager())
	var marked := _marked_scene()
	h.register_spawnable_scene(marked, false)
	var client := await h.add_client()
	var scenes := client.api
	var settled: Array[int] = []
	scenes._scene_core.native_change_settled.connect(settled.append)

	# A client's native change detaches and becomes a server request. The marked
	# destination admits it with no server-side policy code.
	scenes._scene_handle_native_entry(_native_instance(marked))
	var promise: NetwPromise = scenes._scene_core.pending_request
	assert_object(promise).is_not_null()
	await _wait_scene_promise(promise)

	assert_int(promise.code).is_equal(OK)
	assert_object(h.server().api.scene(MARKED_SCENE_NAME)).is_not_null()
	# The capture completion signal fires so a loading screen can tear down.
	assert_array(settled).is_equal([OK])
	await h.teardown()


func test_client_concurrent_captures_move_me() -> void:
	var h := make_unmanaged_harness()
	# CONCURRENT: a client's native change moves only the requester.
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	var marked := _marked_scene()
	h.register_spawnable_scene(marked, false)
	var client := await h.add_client()
	var scenes := client.api

	scenes._scene_handle_native_entry(_native_instance(marked))
	var promise: NetwPromise = scenes._scene_core.pending_request
	assert_object(promise).is_not_null()
	await _wait_scene_promise(promise)

	assert_int(promise.code).is_equal(OK)
	var server_scenes := h.server().api
	assert_object(server_scenes.scene(MARKED_SCENE_NAME)).is_not_null()
	var participant := h.server().api.peer_get_participant(
		client.multiplayer_peer.get_unique_id(),
	)
	assert_object(participant.current_scene) \
			.is_same(server_scenes.scene(MARKED_SCENE_NAME))
	await h.teardown()

# --- Safety cells -----------------------------------------------------------


func test_unmarked_native_change_is_not_hijacked() -> void:
	# Spike D: a native change to an unmarked scene during a session must never
	# be captured. An unmarked scene carries no detach hook, so the replicated
	# session stays intact and the local instance is left alone.
	var source := _level("SafeSource")
	var unmarked := _level("SafeUnmarked")
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	h.register_spawnable_scene(source.packed)
	await h.add_client()
	var scenes := h.server().api
	var before := scenes.scene_list().size()

	var instance := _native_instance(unmarked.packed)
	auto_free(instance)
	add_child(instance)
	await drain_frames(get_tree(), 2)

	# Not captured: the unmarked instance is untouched and no scene was spawned.
	assert_bool(is_instance_valid(instance)).is_true()
	assert_int(instance.process_mode).is_not_equal(Node.PROCESS_MODE_DISABLED)
	assert_object(scenes.scene(unmarked.scene_name)).is_null()
	assert_int(scenes.scene_list().size()).is_equal(before)
	assert_int(h.server().api.state).is_equal(NetwMultiplayer.SessionState.ONLINE)
	await h.teardown()


func test_capture_disabled_under_multiplexed_sessions() -> void:
	# The capture gate is load-bearing: with more than one live session (a
	# tiling rig or the test harness) the mover is unattributable, so a marked
	# scene entering natively stays local instead of hijacking a session.
	var h := make_unmanaged_harness()
	await h.setup_factory(NetwTestSuite.create_scene_manager)
	await h.add_client()
	assert_int(NetwMultiplayer.live_sessions().size()).is_greater(1)

	var marked := _native_instance(_marked_scene())
	auto_free(marked)
	# tree_entered fires the mark hook, which bails on the multiplexed gate.
	add_child(marked)
	await drain_frames(get_tree(), 2)

	assert_bool(is_instance_valid(marked)).is_true()
	assert_int(marked.process_mode).is_not_equal(Node.PROCESS_MODE_DISABLED)
	assert_object(h.server().api.scene(MARKED_SCENE_NAME)).is_null()
	await h.teardown()
