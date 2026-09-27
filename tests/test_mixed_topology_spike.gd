class_name TestMixedTopologySpike
extends NetwTestSuite

const _DT := 1.0 / 60.0

var _original: MultiplayerAPI
var _host_api: NetwMultiplayer
var _client_tree: MultiplayerTree


func before_test() -> void:
	_original = get_tree().get_multiplayer()


func after_test() -> void:
	if is_instance_valid(_client_tree):
		_client_tree.queue_free()
	if _host_api != null and get_tree().get_multiplayer() == _host_api:
		_host_api.multiplayer_peer = null
		_host_api.embed_dispose()
	_host_api = null
	get_tree().set_multiplayer(_original)
	await drain_frames(get_tree(), 3)
	await super.after_test()


func _install_and_host() -> bool:
	_host_api = NetwMultiplayer.make()
	get_tree().set_multiplayer(_host_api)
	var loopback := LocalLoopbackSession.get_shared_session()
	if not loopback.has_live_server():
		loopback.reset()
	_host_api.session_prepare_join(&"host", [])
	_host_api.multiplayer_peer = loopback.get_server_peer()
	return await _pump_until(func() -> bool: return _host_api.is_online)


func _mount_client() -> NetwMultiplayer:
	_client_tree = MultiplayerTree.new()
	_client_tree.name = "SpikeClient"
	_client_tree.desired_role = NetwMultiplayer.ROLE_CLIENT
	_client_tree.auto_host_headless = false
	_client_tree.peer_class = &"LocalMultiplayerPeer"
	add_child(_client_tree)
	return _client_tree.api


func _join_client() -> void:
	var api := _client_tree.api
	api.session_prepare_join(&"client", [])
	api.multiplayer_peer = LocalLoopbackSession \
			.get_shared_session() \
			.create_client_peer()


func _pump_until(cond: Callable, timeout_ms: int = 3000) -> bool:
	var deadline := Time.get_ticks_msec() + timeout_ms
	while Time.get_ticks_msec() < deadline:
		if cond.call():
			return true
		if _host_api != null:
			_host_api.poll()
			if _host_api.has_multiplayer_peer():
				_host_api.poll()
		await get_tree().process_frame
	return cond.call()


func test_root_and_subpath_mounts_coexist_as_live_sessions() -> void:
	_host_api = NetwMultiplayer.make()
	get_tree().set_multiplayer(_host_api)
	var client_api := _mount_client()

	assert_that(get_tree().get_multiplayer()).is_same(_host_api)
	assert_that(get_tree().get_multiplayer(_client_tree.get_path())).is_same(
		client_api,
	)
	assert_that(_host_api.session_is_active()).is_true()
	assert_that(client_api.session_is_active()).is_true()
	assert_that(NetwMultiplayer.session_get_all()).contains(
		[_host_api, client_api],
	)
	assert_that(NetwMultiplayer.of(_client_tree)).is_same(client_api)


func test_tree_less_root_comes_online_as_a_host() -> void:
	var online := await _install_and_host()
	assert_bool(online).override_failure_message(
		"tree-less root session never came online through a peer assignment",
	).is_true()
	assert_that(_host_api.is_host).is_true()
	assert_that(_host_api.get_unique_id()).is_equal(1)


func test_subpath_client_is_admitted_across_the_mount_boundary() -> void:
	var online := await _install_and_host()
	assert_bool(online).is_true()

	var client_api := _mount_client()
	_join_client()

	var admitted := await _pump_until(
		func() -> bool: return client_api.local_player != null
	)
	assert_bool(admitted).override_failure_message(
		"subpath client was never admitted to its own session",
	).is_true()

	var client_id := client_api.get_unique_id()
	var host_sees_client := await _pump_until(
		func() -> bool: return _host_api.peer_get_player(client_id) != null
	)
	assert_bool(host_sees_client).override_failure_message(
		"root host admitted no roster row for the subpath client",
	).is_true()
