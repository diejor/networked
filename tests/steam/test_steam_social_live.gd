extends GdUnitTestSuite

const WAIT_MS := 10000


class Gate extends Node:
	func admit(_request: NetwJoinRequest) -> Error:
		return OK


func before(
		do_skip = OS.get_environment("NETW_STEAM_TEST") != "1",
		skip_reason = "Set NETW_STEAM_TEST=1 with a logged-in Steam client.",
) -> void:
	pass


func presence() -> String:
	var steam := Engine.get_singleton(&"Steam")
	return steam.getFriendRichPresence(steam.getSteamID(), "connect")


func test_an_enet_session_shows_join_game_while_joinable() -> void:
	var viewport := MultiplayerViewport.new()
	var social := SteamSocial.new()
	social.spacewar_when_unset = true
	var gate := Gate.new()
	viewport.add_child(social)
	viewport.add_child(gate)
	add_child(viewport)
	Netw.configure_admission(gate.admit)
	var session := Netw.session(viewport)

	assert_bool(social.invite_friend(76561197960287930)).is_false()
	Netw.prepare_join(gate, &"host")
	await session.connection.host(&"ENetMultiplayerPeer", { "port": 0 }).wait()
	var started := Time.get_ticks_msec()
	while session.local_player == null \
			and Time.get_ticks_msec() - started < WAIT_MS:
		await get_tree().process_frame

	var address := session.connection.join_address
	assert_str(address).is_not_empty()
	assert_str(presence()).is_equal("+connect ENetMultiplayerPeer|" + address)

	await session.leave().wait()
	assert_str(presence()).is_empty()
	viewport.queue_free()
	await get_tree().process_frame


func test_two_socials_route_one_join_game_click_once() -> void:
	var invited: Array[NetwEndpoint] = []
	var viewports: Array[MultiplayerViewport] = []
	for index in 2:
		var viewport := MultiplayerViewport.new()
		var social := SteamSocial.new()
		social.spacewar_when_unset = true
		viewport.add_child(social)
		add_child(viewport)
		Netw.session(viewport).connection.invited.connect(invited.append)
		viewports.append(viewport)

	Engine.get_singleton(&"Steam").join_game_requested.emit(
		0,
		"+connect ENetMultiplayerPeer|127.0.0.1:1",
	)

	assert_int(invited.size()).is_equal(1)
	for viewport in viewports:
		viewport.queue_free()
	await get_tree().process_frame
