class_name TestNakamaRoom
extends NetwTestSuite

const TIMEOUT := 15.0

var trees: Array[MultiplayerViewport] = []


func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func after_test() -> void:
	for tree in trees:
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	trees.clear()
	await super.after_test()


func test_players_arriving_one_by_one_share_one_server() -> void:
	var room := fresh_room()
	var group := await signed_in(["ana", "ben", "cai"])
	for tree in group:
		assert_int(await arrive(tree, room)).is_equal(OK)

	if not await wait_until(room_whole.bind(group), "the room"):
		return
	assert_int(servers_in(group)).is_equal(1)
	assert_bool(group[0].multiplayer.is_server()).is_true()


func test_players_arriving_at_once_share_one_server() -> void:
	var room := fresh_room()
	var group := await signed_in(["ana", "ben", "cai", "dia"])
	var attempts: Array[NetwPromise] = []
	for tree in group:
		attempts.append(start_arriving(tree, room))
	for attempt in attempts:
		if not attempt.is_settled:
			await attempt.wait()
		assert_int(attempt.code).is_equal(OK)

	if not await wait_until(room_whole.bind(group), "the room"):
		return
	assert_int(servers_in(group)).is_equal(1)


func test_the_next_arrival_serves_a_room_its_server_left() -> void:
	var room := fresh_room()
	var group := await signed_in(["ana", "ben", "cai"])
	for tree in group.slice(0, 2):
		assert_int(await arrive(tree, room)).is_equal(OK)
	if not await wait_until(room_whole.bind(group.slice(0, 2)), "the room"):
		return

	await Netw.session(group[0]).leave().wait()
	assert_int(await arrive(group[2], room)).is_equal(OK)

	assert_bool(group[2].multiplayer.is_server()).is_true()
	assert_int(await arrive(group[0], room)).is_equal(OK)
	await wait_until(room_whole.bind([group[0], group[2]]), "the second room")


func test_an_arrival_serves_once_the_players_before_it_leave() -> void:
	var room := fresh_room()
	var group := await signed_in(["ana", "ben"])
	var socket := NakamaTestSupport.account(group[0]).socket
	var lingering = await socket.create_match_async(room.trim_prefix("room:"))
	assert_bool(lingering.is_exception()).is_false()
	var attempt := start_arriving(group[1], room)
	await get_tree().create_timer(1.0).timeout
	assert_bool(attempt.is_settled).is_false()

	await socket.leave_match_async(lingering.match_id)
	await attempt.wait()

	assert_int(attempt.code).is_equal(OK)
	assert_bool(group[1].multiplayer.is_server()).is_true()


func test_a_room_keeps_its_players_when_the_server_is_lost() -> void:
	var room := fresh_room()
	var group := await signed_in(["ana", "ben", "cai", "dia"])
	for tree in group:
		NakamaTestSupport.directory(tree).migrate_on_host_loss = true
	for tree in group.slice(0, 3):
		assert_int(await arrive(tree, room)).is_equal(OK)
	if not await wait_until(room_whole.bind(group.slice(0, 3)), "the room"):
		return

	trees.erase(group[0])
	await NakamaTestSupport.stop_tree(group[0])
	var survivors := group.slice(1, 3)
	if not await wait_until(room_whole.bind(survivors), "the survivors"):
		return
	assert_int(servers_in(survivors)).is_equal(1)

	assert_int(await arrive(group[3], room)).is_equal(OK)
	await wait_until(room_whole.bind(group.slice(1, 4)), "a late arrival")
	assert_bool(group[3].multiplayer.is_server()).is_false()


func fresh_room() -> String:
	return "room:netw-test:%d-%d" % [Time.get_unix_time_from_system(), randi()]


func signed_in(names: Array[String]) -> Array[MultiplayerViewport]:
	var group: Array[MultiplayerViewport] = []
	for username in names:
		var tree := NakamaTestSupport.make_client_tree(self, username)
		trees.append(tree)
		assert_int(await NakamaTestSupport.sign_in(tree, username)).is_equal(OK)
		group.append(tree)
	return group


func start_arriving(tree: MultiplayerViewport, room: String) -> NetwPromise:
	var connection := Netw.session(tree).connection
	tree.api.session_prepare_join(StringName(tree.name), [])
	return connection.join(connection.add(NakamaTestSupport.PEER_CLASS, room))


func arrive(tree: MultiplayerViewport, room: String) -> Error:
	var attempt := start_arriving(tree, room)
	if not attempt.is_settled:
		await attempt.wait()
	return attempt.code as Error


func room_whole(group: Array) -> bool:
	for tree: MultiplayerViewport in group:
		if not tree.api.is_online:
			return false
		if Netw.session(tree).players.size() != group.size():
			return false
		if tree.multiplayer.get_peers().size() != group.size() - 1:
			return false
	return true


func servers_in(group: Array) -> int:
	var servers := 0
	for tree: MultiplayerViewport in group:
		if tree.multiplayer.is_server():
			servers += 1
	return servers


func wait_until(condition: Callable, label: String, timeout: float = TIMEOUT) -> bool:
	var deadline := Time.get_ticks_msec() + int(timeout * 1000.0)
	while not condition.call():
		if Time.get_ticks_msec() > deadline:
			assert_bool(false) \
					.override_failure_message("Timed out waiting for %s." % label) \
					.is_true()
			return false
		await get_tree().process_frame
	return true
