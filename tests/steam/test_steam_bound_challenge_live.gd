extends GdUnitTestSuite

const WAIT_SECONDS := 300.0


class Gate extends Node:
	var requests: Array[NetwJoinRequest] = []


	func prove(value: String) -> String:
		return "proof:" + value


	func admit(request: NetwJoinRequest) -> Error:
		requests.append(request)
		if not request.challenge_bound:
			return ERR_UNAUTHORIZED
		if request.proof != "proof:" + request.challenge:
			return ERR_UNAUTHORIZED
		return OK


func before(
		do_skip = OS.get_environment("NETW_STEAM_BOUND").is_empty(),
		skip_reason = "Set NETW_STEAM_BOUND=host on one Steam account, then "
		+ "NETW_STEAM_BOUND=<lobby id> on a second account.",
) -> void:
	pass


func test_steam_binds_the_challenge_to_the_host() -> void:
	var role := OS.get_environment("NETW_STEAM_BOUND")
	var viewport := MultiplayerViewport.new()
	var directory := SteamLobbyDirectory.new()
	directory.spacewar_when_unset = true
	var gate := Gate.new()
	viewport.add_child(directory)
	viewport.add_child(gate)
	add_child(viewport)
	Netw.configure_admission(gate.admit).proof(gate.prove)
	var session: NetwSession = Netw.session(viewport)
	if role == "host":
		var hosted: NetwPromise = session.connection.host(
			SteamMultiplayerPeer,
			{
				"name": "Networked bound challenge",
				"visibility": NetwServerInfo.VISIBILITY_PUBLIC,
			},
		)
		await hosted.wait()
		assert_bool(hosted.is_failed).is_false()
		print("NETW_STEAM_BOUND=%d" % directory.lobby_id)
		var waited := 0.0
		while gate.requests.is_empty() and waited < WAIT_SECONDS:
			await get_tree().create_timer(0.5).timeout
			waited += 0.5
		assert_array(gate.requests).is_not_empty()
		var remote: NetwJoinRequest = gate.requests[0]
		assert_bool(remote.challenge_bound).is_true()
		assert_str(remote.proof).is_equal("proof:" + remote.challenge)
		await get_tree().create_timer(5.0).timeout
	else:
		var endpoint: NetwEndpoint = session.connection.add(
			SteamMultiplayerPeer,
			role,
		)
		Netw.prepare_join(gate, &"bound")
		var connected: NetwPromise = session.connection.join(endpoint)
		await connected.wait()
		assert_bool(connected.is_failed).is_false()
		var waited := 0.0
		while session.local_player == null and waited < WAIT_SECONDS:
			await get_tree().create_timer(0.5).timeout
			waited += 0.5
		assert_object(session.local_player).is_not_null()
	await session.leave().wait()
	viewport.queue_free()
	await get_tree().process_frame
