extends GdUnitTestSuite

func before(
		do_skip = OS.get_environment("NETW_STEAM_TEST") != "1",
		skip_reason = "Set NETW_STEAM_TEST=1 with a logged-in Steam client.",
) -> void:
	pass


func test_host_uses_the_connection() -> void:
	var viewport := MultiplayerViewport.new()
	var directory := SteamLobbyDirectory.new()
	directory.spacewar_when_unset = true
	var social := SteamSocial.new()
	social.spacewar_when_unset = true
	viewport.add_child(directory)
	viewport.add_child(social)
	add_child(viewport)
	var session: NetwSession = Netw.session(viewport)
	var result: NetwPromise = session.connection.host(
		SteamMultiplayerPeer,
		{
			"name": "Networked live test",
			"visibility": NetwServerInfo.VISIBILITY_PRIVATE,
		},
	)
	await result.wait()
	assert_bool(result.is_failed).is_false()
	assert_int(directory.lobby_id).is_greater(0)
	assert_str(session.connection.join_address) \
			.is_equal(str(directory.lobby_id))
	var id: int = Steam.getSteamID()
	assert_array(directory.members).contains([id])
	assert_int(directory.member_of(1)).is_equal(id)
	assert_int(directory.peer_of(id)).is_equal(1)
	assert_bool(directory.set_data("map", "arena")).is_true()
	assert_str(directory.data("map")).is_equal("arena")
	directory.set_member_data("ready", "yes")
	assert_str(directory.member_data(id, "ready")).is_equal("yes")

	var own := social.profile(id)
	assert_object(social.profile(id)).is_same(own)
	assert_str(own.display_name).is_equal(Steam.getPersonaName())
	var started := Time.get_ticks_msec()
	while own.avatar == null and Time.get_ticks_msec() - started < 5000:
		await get_tree().process_frame
	assert_object(own.avatar).is_not_null()
	assert_int(own.avatar.get_width() if own.avatar else 0).is_equal(64)
	await session.leave().wait()
	assert_int(directory.lobby_id).is_equal(0)
	viewport.queue_free()
	await get_tree().process_frame
