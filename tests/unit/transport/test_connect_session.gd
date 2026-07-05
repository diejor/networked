## Unit tests for [ConnectSession]: target persistence, directory dispatch, and
## explicit failure emission.
@tool
class_name TestConnectSession
extends NetwTestSuite

class _UnavailableBackend:
	extends BackendPeer

	func create_host_peer(
			_tree: MultiplayerTree,
			_options: LobbyDirectory.HostOptions = null,
	) -> MultiplayerPeer:
		return null


	func create_join_peer(
			_tree: MultiplayerTree,
			_address: String,
			_username: String = "",
	) -> MultiplayerPeer:
		return null


	func is_available() -> bool:
		return false


class _ProgressBackend:
	extends BackendPeer

	func create_host_peer(
			_tree: MultiplayerTree,
			_options: LobbyDirectory.HostOptions = null,
	) -> MultiplayerPeer:
		return null


	func create_join_peer(
			_tree: MultiplayerTree,
			_address: String,
			_username: String = "",
	) -> MultiplayerPeer:
		connect_progress.emit(&"connecting", "Mock progress", 0.5)
		return null


class _MockDirectory:
	extends LobbyDirectory

	var canned_lobbies: Array[LobbyDirectory.LobbyInfo] = []
	var list_calls: int = 0


	func list_lobbies() -> void:
		list_calls += 1
		lobby_list_updated.emit(canned_lobbies)


	func leave_lobby() -> void:
		pass


	func make_join_target(lobby: LobbyDirectory.LobbyInfo) -> JoinTarget:
		var target := JoinTarget.new()
		target.address = str(lobby.id)
		target.backend = ENetBackend.new()
		target.display_name = lobby.lobby_name
		return target


	func host_lobby(_options: LobbyDirectory.HostOptions) -> MultiplayerPeer:
		return null


	func join_lobby_peer(_lobby_id: int) -> MultiplayerPeer:
		return null


func _temp_path() -> String:
	return "user://_test_connect_session_%d.tres" % Time.get_ticks_usec()


func _make_target(address: String = "127.0.0.1") -> JoinTarget:
	var target := JoinTarget.new()
	target.address = address
	target.backend = ENetBackend.new()
	target.display_name = "T_" + address
	return target


func test_saved_target_flow() -> void:
	var path := _temp_path()
	var session := ConnectSession.new()
	add_child(session)
	session.server_list_path = path
	var captured: Array = []
	session.target_added.connect(func(t): captured.append(t))

	var target := _make_target()
	session.add_target(target, false)

	assert_int(captured.size()).is_equal(1)
	assert_that(captured[0]).is_same(target)
	assert_bool(FileAccess.file_exists(path)).is_false()
	session.queue_free()

	session = ConnectSession.new()
	add_child(session)
	session.server_list_path = path
	session.add_target(_make_target("10.0.0.1"), true)
	assert_bool(FileAccess.file_exists(path)).is_true()

	var loaded_session := ConnectSession.new()
	add_child(loaded_session)
	var added: Array = []
	loaded_session.target_added.connect(func(t): added.append(t))
	loaded_session.load_server_list(path)

	var loaded_targets := loaded_session.get_saved_targets()
	assert_int(loaded_targets.size()).is_equal(1)
	assert_that(loaded_targets[0].address).is_equal("10.0.0.1")
	assert_int(added.size()).is_equal(1)

	var removed: Array = []
	loaded_session.target_removed.connect(func(t): removed.append(t))
	loaded_session.remove_target(loaded_targets[0])

	assert_int(removed.size()).is_equal(1)
	assert_that(removed[0]).is_same(loaded_targets[0])
	assert_int(loaded_session.get_saved_targets().size()).is_equal(0)

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	session.queue_free()
	loaded_session.queue_free()


func test_directory_refresh_flow() -> void:
	var session := ConnectSession.new()
	add_child(session)

	var directory := _MockDirectory.new()
	add_child(directory)
	var info := LobbyDirectory.LobbyInfo.make(42, "Mock Lobby", 2, 8, { })
	directory.canned_lobbies = [info]

	var directory_emitted: Array = []
	session.directory_list_updated.connect(
		func(id, _l): directory_emitted.append(id)
	)
	var added: Array = []
	session.target_added.connect(func(t): added.append(t))

	session.register_directory(&"mock", directory)
	session.refresh()

	assert_int(directory.list_calls).is_equal(1)
	assert_int(directory_emitted.size()).is_equal(1)
	assert_that(directory_emitted[0]).is_equal(&"mock")
	assert_int(added.size()).is_equal(1)
	assert_int(session.get_discovered_targets(&"mock").size()).is_equal(1)

	directory.queue_free()
	session.queue_free()

	session = ConnectSession.new()
	add_child(session)
	var target := JoinTarget.new()
	target.backend = _UnavailableBackend.new()
	target.address = "1.2.3.4"
	target.display_name = "Web-only-host"
	session.add_target(target)

	var updates: Array = []
	session.target_updated.connect(func(t, _r): updates.append(t))

	session.refresh()
	await get_tree().process_frame
	await get_tree().process_frame

	assert_int(updates.size()).is_equal(0)
	session.queue_free()


func test_join_failure_and_progress_flow() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var failed: Array = []
	session.join_failed.connect(func(_t, result): failed.append(result))

	var target := _make_target()
	var payload := JoinPayload.new()
	payload.username = &"valeria"
	var err := await session.join(target, payload)

	assert_int(err).is_equal(ERR_UNCONFIGURED)
	assert_int(failed.size()).is_equal(1)
	assert_bool(failed[0].message.contains("MultiplayerTree")).is_true()

	var tree := MultiplayerTree.new()
	add_child(tree)
	session.bind_tree(tree)
	target = JoinTarget.new()
	target.backend = null

	err = await session.join(target, payload)
	assert_int(err).is_equal(ERR_INVALID_PARAMETER)
	assert_int(failed.size()).is_equal(2)
	assert_bool(failed[1].message.contains("failed")).is_true()

	target = _make_target()
	target.backend = _ProgressBackend.new()
	var progress: Array = []
	session.join_progress.connect(
		func(t, step, message, ratio):
			progress.append([t, step, message, ratio])
	)

	err = await session.join(target, payload)

	assert_int(err).is_equal(ERR_CANT_CONNECT)
	assert_int(progress.size()).is_equal(1)
	assert_that(progress[0][0]).is_same(target)
	assert_str(progress[0][1]).is_equal(&"connecting")
	assert_str(progress[0][2]).is_equal("Mock progress")
	assert_float(progress[0][3]).is_equal(0.5)

	tree.backend = null
	target.backend = null
	tree.queue_free()
	session.queue_free()


func test_host_failure_flow() -> void:
	var session := ConnectSession.new()
	add_child(session)
	var captured: Array = []
	session.host_failed.connect(func(reason): captured.append(reason))

	var config := ConnectHostConfig.new()
	config.backend = ENetBackend.new()
	var payload := JoinPayload.new()
	payload.username = &"valeria"
	var err := await session.host(config, payload)

	assert_int(err).is_equal(ERR_UNCONFIGURED)
	assert_int(captured.size()).is_equal(1)
	assert_bool(captured[0].contains("MultiplayerTree")).is_true()

	var tree := MultiplayerTree.new()
	add_child(tree)
	session.bind_tree(tree)

	config = ConnectHostConfig.new()
	err = await session.host(config, payload)

	assert_int(err).is_equal(ERR_INVALID_PARAMETER)
	assert_int(captured.size()).is_equal(2)
	tree.queue_free()
	session.queue_free()
