extends GdUnitTestSuite

const SteamStart := preload("res://addons/networked_steam/steam_start.gd")

const WAIT_MS := 45000

const FRIEND := 76561197960287930


class Gate extends Node:
	func admit(_request: NetwJoinRequest) -> Error:
		return OK


var viewport: MultiplayerViewport

var steam: SteamSocial

var opened: Array[int]


func before(
		do_skip = OS.get_environment("NETW_STEAM_TEST") != "1",
		skip_reason = "Set NETW_STEAM_TEST=1 with a logged-in Steam client.",
) -> void:
	pass


func before_test() -> void:
	viewport = MultiplayerViewport.new()
	steam = SteamSocial.new()
	steam.spacewar_when_unset = true
	viewport.add_child(steam)
	add_child(viewport)
	Steam.lobby_created.connect(on_lobby_created)


func after_test() -> void:
	Steam.lobby_created.disconnect(on_lobby_created)
	viewport.queue_free()
	await get_tree().process_frame
	for lobby in opened:
		Steam.leaveLobby(lobby)
	opened.clear()


func on_lobby_created(result: int, lobby: int) -> void:
	if result == Steam.RESULT_OK:
		opened.append(lobby)


func wait_until(condition: Callable) -> void:
	var started := Time.get_ticks_msec()
	while not condition.call() and Time.get_ticks_msec() - started < WAIT_MS:
		await get_tree().process_frame


func open_lobby(type: int) -> int:
	for attempt in 3:
		var made: Array = await SteamStart.create_lobby(type, 4)
		if made[0] == Steam.RESULT_OK:
			return made[1]
	return 0


func host() -> NetwSession:
	var gate := Gate.new()
	viewport.add_child(gate)
	Netw.configure_admission(gate.admit)
	var session := Netw.session(viewport)
	Netw.prepare_join(gate, &"host")
	await session.connection.host(&"ENetMultiplayerPeer", { "port": 0 }).wait()
	await wait_until(func() -> bool: return session.local_player != null)
	return session


func test_the_leader_writes_its_server_into_a_lobby_the_first_invitation_opens() -> void:
	var party := steam.party
	var own: int = Steam.getSteamID()
	assert_array(party.members).contains_exactly([own])
	assert_int(party.leader).is_equal(own)
	assert_array(opened).is_empty()

	party.invite(FRIEND)
	await wait_until(func() -> bool: return not opened.is_empty())
	assert_array(opened).has_size(1)
	var lobby := opened[0]
	assert_str(Steam.getLobbyData(lobby, "netw")).is_equal("party")
	assert_int(Steam.getLobbyOwner(lobby)).is_equal(own)
	assert_array(party.members).contains_exactly([own])
	assert_int(party.leader).is_equal(own)

	var session := await host()
	var server := "ENetMultiplayerPeer|" + session.connection.join_address
	await wait_until(
		func() -> bool: return Steam.getLobbyData(lobby, "join") == server,
	)
	assert_str(Steam.getLobbyData(lobby, "join")).is_equal(server)
	await session.leave().wait()
	await wait_until(
		func() -> bool: return Steam.getLobbyData(lobby, "join").is_empty(),
	)
	assert_str(Steam.getLobbyData(lobby, "join")).is_empty()


func test_an_accepted_party_invitation_moves_the_party_there() -> void:
	var party := steam.party
	var other := await open_lobby(Steam.LOBBY_TYPE_INVISIBLE)
	assert_int(other).is_not_zero()
	Steam.setLobbyData(other, "netw", "party")
	var heard: Array[int] = []
	party.invited.connect(heard.append)

	Steam.lobby_invite.emit(FRIEND, other, Steam.getAppID())
	await wait_until(func() -> bool: return not heard.is_empty())
	assert_array(heard).contains_exactly([FRIEND])
	assert_array(party.invitations).contains_exactly([FRIEND])

	Steam.join_requested.emit(other, FRIEND)
	await wait_until(func() -> bool: return party.invitations.is_empty())
	assert_array(party.invitations).is_empty()
	var session := await host()
	var server := "ENetMultiplayerPeer|" + session.connection.join_address
	await wait_until(
		func() -> bool: return Steam.getLobbyData(other, "join") == server,
	)
	assert_str(Steam.getLobbyData(other, "join")).is_equal(server)
	await session.leave().wait()


func test_a_match_lobby_join_reaches_the_connection_and_not_the_party() -> void:
	var directory := SteamLobbyDirectory.new()
	directory.spacewar_when_unset = true
	viewport.add_child(directory)
	var match_lobby := await open_lobby(Steam.LOBBY_TYPE_FRIENDS_ONLY)
	assert_int(match_lobby).is_not_zero()
	var invited: Array[NetwEndpoint] = []
	Netw.session(viewport).connection.invited.connect(invited.append)

	var started := Time.get_ticks_msec()
	Steam.lobby_invite.emit(FRIEND, match_lobby, Steam.getAppID())
	Steam.join_requested.emit(match_lobby, FRIEND)
	await wait_until(func() -> bool: return not invited.is_empty())
	assert_int(Time.get_ticks_msec() - started).is_less(2000)
	assert_array(invited).has_size(1)
	assert_str(invited[0].peer_class).is_equal("SteamMultiplayerPeer")
	assert_str(invited[0].address).is_equal(str(match_lobby))
	assert_array(steam.party.invitations).is_empty()


func test_lobbies_opened_together_each_get_their_own() -> void:
	var answers: Array[int] = []
	var take := func() -> void:
		answers.append(await open_lobby(Steam.LOBBY_TYPE_INVISIBLE))
	take.call()
	take.call()
	await wait_until(func() -> bool: return answers.size() == 2)
	assert_array(answers).has_size(2)
	assert_array(answers).not_contains([0])
	assert_int(answers.front()).is_not_equal(answers.back())
