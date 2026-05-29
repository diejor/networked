## Unit tests for [ConnectSession]: target add/remove, opt-in
## persistence, provider list dispatch, and explicit failure
## emission. Network-dependent paths (host / join success) live in
## integration tests; here we cover what's reachable without a real
## MultiplayerTree or backend.
class_name TestConnectSession
extends NetwTestSuite


class _MockProvider:
	extends LobbyProvider
	var create_called_with: String = ""
	var join_called_with: Variant = null
	var list_calls: int = 0
	var canned_lobbies: Array[LobbyInfo] = []
	var fail_reason: String = ""

	func create_lobby(server_name: String) -> void:
		create_called_with = server_name
		if not fail_reason.is_empty():
			lobby_join_failed.emit(fail_reason)
		else:
			lobby_created.emit(1)

	func join_lobby(lobby_id) -> void:
		join_called_with = lobby_id
		if not fail_reason.is_empty():
			lobby_join_failed.emit(fail_reason)

	func list_lobbies() -> void:
		list_calls += 1
		lobby_list_updated.emit(canned_lobbies)

	func leave_lobby() -> void:
		pass

	func get_peer() -> MultiplayerPeer:
		return null


func _temp_path() -> String:
	return "user://_test_connect_session_%d.tres" % Time.get_ticks_usec()


func _make_target(address: String = "127.0.0.1") -> JoinTarget:
	var target := JoinTarget.new()
	target.address = address
	target.backend = ENetBackend.new()
	target.display_name = "T_" + address
	return target


func test_add_target_emits_target_added() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var captured: Array = []
	session.target_added.connect(func(t): captured.append(t))

	var target := _make_target()
	session.add_target(target)

	assert_int(captured.size()).is_equal(1)
	assert_that(captured[0]).is_same(target)
	session.queue_free()


func test_add_target_persist_false_does_not_write() -> void:
	var path := _temp_path()
	var session := ConnectSession.new()
	add_child(session)
	session.server_list_path = path

	session.add_target(_make_target(), false)
	assert_bool(FileAccess.file_exists(path)).is_false()
	session.queue_free()


func test_add_target_persist_true_writes_to_disk() -> void:
	var path := _temp_path()
	var session := ConnectSession.new()
	add_child(session)
	session.server_list_path = path

	session.add_target(_make_target("10.0.0.1"), true)
	assert_bool(FileAccess.file_exists(path)).is_true()

	var loaded := ServerList.load_or_new(path)
	assert_int(loaded.targets.size()).is_equal(1)
	assert_that(loaded.targets[0].address).is_equal("10.0.0.1")
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	session.queue_free()


func test_load_server_list_emits_target_added_per_entry() -> void:
	var path := _temp_path()
	var seed_list := ServerList.new()
	seed_list.targets = [_make_target("a"), _make_target("b")]
	ServerList.save(seed_list, path)

	var session := ConnectSession.new()
	add_child(session)
	var added: Array = []
	session.target_added.connect(func(t): added.append(t))

	session.load_server_list(path)
	assert_int(added.size()).is_equal(2)
	assert_int(session.get_direct_targets().size()).is_equal(2)
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	session.queue_free()


func test_remove_target_emits_and_clears_result() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var target := _make_target()
	session.add_target(target)

	var removed: Array = []
	session.target_removed.connect(func(t): removed.append(t))
	session.remove_target(target)

	assert_int(removed.size()).is_equal(1)
	assert_that(removed[0]).is_same(target)
	assert_int(session.get_direct_targets().size()).is_equal(0)
	session.queue_free()


func test_register_provider_and_refresh_dispatches_provider_targets() -> void:
	var session := ConnectSession.new()
	add_child(session)

	var provider := _MockProvider.new()
	add_child(provider)
	var info := LobbyInfo.make(42, "Mock Lobby", 2, 8, {})
	provider.canned_lobbies = [info]

	var provider_emitted: Array = []
	session.provider_list_updated.connect(
		func(id, _l): provider_emitted.append(id)
	)
	var added: Array = []
	session.target_added.connect(func(t): added.append(t))

	session.register_provider(&"mock", provider)
	session.refresh()

	assert_int(provider.list_calls).is_equal(1)
	assert_int(provider_emitted.size()).is_equal(1)
	assert_that(provider_emitted[0]).is_equal(&"mock")
	assert_int(added.size()).is_equal(1)
	assert_int(session.get_provider_targets(&"mock").size()).is_equal(1)

	provider.queue_free()
	session.queue_free()


func test_join_without_tree_emits_join_failed() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var captured_reason: Array = []
	session.join_failed.connect(
		func(_t, reason): captured_reason.append(reason)
	)

	var target := _make_target()
	var payload := JoinPayload.new()
	payload.username = &"alice"
	var err := await session.join(target, payload)

	assert_int(err).is_equal(ERR_UNCONFIGURED)
	assert_int(captured_reason.size()).is_equal(1)
	assert_bool(captured_reason[0].contains("MultiplayerTree")).is_true()
	session.queue_free()


func test_join_provider_missing_emits_join_failed() -> void:
	var session := ConnectSession.new()
	add_child(session)
	# Bind a fresh tree so the no-tree branch doesn't fire first.
	var tree := MultiplayerTree.new()
	add_child(tree)
	session.bind_tree(tree)

	var target := JoinTarget.new()
	target.provider_id = &"absent"
	target.remote_id = 1

	var captured: Array = []
	session.join_failed.connect(
		func(_t, reason): captured.append(reason)
	)

	var payload := JoinPayload.new()
	payload.username = &"alice"
	var err := await session.join(target, payload)

	assert_int(err).is_equal(ERR_DOES_NOT_EXIST)
	assert_int(captured.size()).is_equal(1)
	assert_bool(captured[0].contains("absent")).is_true()

	tree.queue_free()
	session.queue_free()


func test_host_without_tree_emits_host_failed() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var captured: Array = []
	session.host_failed.connect(func(reason): captured.append(reason))

	var config := ConnectHostConfig.new()
	config.backend = ENetBackend.new()
	var payload := JoinPayload.new()
	payload.username = &"alice"
	var err := await session.host(config, payload)

	assert_int(err).is_equal(ERR_UNCONFIGURED)
	assert_int(captured.size()).is_equal(1)
	assert_bool(captured[0].contains("MultiplayerTree")).is_true()
	session.queue_free()


func test_host_missing_backend_emits_host_failed() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var tree := MultiplayerTree.new()
	add_child(tree)
	session.bind_tree(tree)

	var captured: Array = []
	session.host_failed.connect(func(reason): captured.append(reason))

	var config := ConnectHostConfig.new()
	# No backend, no provider_id -> direct path with null template.
	var payload := JoinPayload.new()
	payload.username = &"alice"
	var err := await session.host(config, payload)

	assert_int(err).is_equal(ERR_INVALID_PARAMETER)
	assert_int(captured.size()).is_equal(1)
	tree.queue_free()
	session.queue_free()
