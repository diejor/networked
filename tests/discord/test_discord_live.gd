extends GdUnitTestSuite

const TEST_APPLICATION_ID := 1561065725962092666
const WAIT_SECONDS := 300.0
const ENET := &"ENetMultiplayerPeer"


class Gate extends Node:
	var social: DiscordSocial
	var application_id: int


	func prove(challenge: String) -> PackedByteArray:
		return await social.prove(challenge)


	func admit(request: NetwJoinRequest) -> Error:
		return await DiscordIdToken.admit(request, application_id)


var role: String
var viewport: MultiplayerViewport
var social: DiscordSocial
var party: DiscordParty
var session: NetwSession
var gate: Gate
var dropped_call: bool


func before(
		do_skip = OS.get_environment("NETW_DISCORD_LIVE").is_empty(),
		skip_reason = "Set NETW_DISCORD_LIVE=host and NETW_DISCORD_LIVE_FRIEND "
		+ "on the leader's account, then NETW_DISCORD_LIVE=<leader user id> "
		+ "on the friend's account.",
) -> void:
	role = OS.get_environment("NETW_DISCORD_LIVE")


func step(label: String, condition: Callable, seconds := WAIT_SECONDS) -> bool:
	print("NETW_DISCORD_LIVE waiting: ", label)
	var started := Time.get_ticks_msec()
	while not condition.call() \
			and Time.get_ticks_msec() - started < seconds * 1000.0:
		await get_tree().create_timer(0.25).timeout

	var done: bool = condition.call()
	print(
		"NETW_DISCORD_LIVE ",
		"done: " if done else "TIMED OUT: ",
		label,
		" (%.1fs)" % ((Time.get_ticks_msec() - started) / 1000.0),
	)
	assert_bool(done).override_failure_message(label).is_true()
	return done


func seconds(duration: float) -> void:
	await get_tree().create_timer(duration).timeout


func watch(signal_name: String) -> Callable:
	return func(...args: Array) -> void:
		print("NETW_DISCORD_LIVE signal: ", signal_name, " ", args)


func account_of(user_id: int) -> String:
	return "discord:%d" % user_id


func pair(other: int) -> Array[int]:
	var both: Array[int] = [social.user_id, other]
	both.sort()
	return both


func holds_no_lobby() -> bool:
	return social.client.get_lobby_ids().is_empty()


func test_discord_live_round_trip(timeout := 1800000) -> void:
	var application_id := int(
		OS.get_environment("NETW_DISCORD_APPLICATION_ID"),
	)
	if application_id == 0:
		application_id = TEST_APPLICATION_ID
	viewport = MultiplayerViewport.new()
	social = DiscordSocial.new()
	social.application_id = application_id
	party = social.party
	gate = Gate.new()
	gate.social = social
	gate.application_id = application_id
	viewport.add_child(social)
	viewport.add_child(gate)
	add_child(viewport)
	viewport.api.auth_timeout = WAIT_SECONDS
	Netw.configure_admission(gate.admit).proof(gate.prove)
	session = Netw.session(viewport)
	party.invited.connect(watch("party.invited"))
	party.join_requested.connect(watch("party.join_requested"))
	session.connection.invited.connect(watch("connection.invited"))
	session.connection.failed.connect(watch("connection.failed"))
	party.changed.connect(_on_party_changed)

	var started := Time.get_ticks_msec()
	var signing_in: NetwPromise = social.sign_in()
	await signing_in.wait()
	assert_bool(signing_in.is_failed).override_failure_message(
		signing_in.detail,
	).is_false()
	print(
		"NETW_DISCORD_LIVE signed in after %.1fs, under 5s had no page"
		% ((Time.get_ticks_msec() - started) / 1000.0),
	)
	assert_str(social.profile(social.user_id).display_name).is_not_empty()
	assert_array(party.members).contains_exactly([social.user_id])
	assert_int(party.leader).is_equal(social.user_id)
	assert_str(party.join_secret).is_not_empty()
	if role == "host":
		await lead(int(OS.get_environment("NETW_DISCORD_LIVE_FRIEND")))
	else:
		await follow(int(role))
	await session.leave().wait()
	viewport.queue_free()
	await get_tree().process_frame


func _on_party_changed() -> void:
	print(
		"NETW_DISCORD_LIVE party: members=%s leader=%d in_call=%s"
		% [party.members, party.leader, party.in_call],
	)
	dropped_call = dropped_call or not party.in_call


func host() -> void:
	Netw.prepare_join(gate, &"leader")
	var hosted := session.connection.host(ENET, { "port": 0 })
	await hosted.wait()
	assert_bool(hosted.is_failed).is_false()
	var chosen := OS.get_environment("NETW_DISCORD_LIVE_ADDRESS")
	for local: String in IP.get_local_addresses():
		if chosen.is_empty() and local.begins_with("192.168."):
			chosen = local
	var port := session.connection.current_endpoint.address.get_slice(":", 1)
	session.connection.join_address = chosen + ":" + port


func lead(friend: int) -> void:
	var own := social.user_id
	assert_array(social.friends).contains([friend])
	await seconds(5.0)
	print("NETW_DISCORD_LIVE=%d" % own)
	var asked := func() -> bool: return friend in party.join_requests
	if not await step("ask: the friend's join request arrives", asked):
		return
	party.invite(friend)
	var together := func() -> bool: return party.members == pair(friend)
	if not await step("ask: the friend is in the party", together):
		return
	assert_int(party.leader).is_equal(own)

	var listening := func() -> bool:
		return party.in_call and party.voice_state(friend) \
				== DiscordParty.VoiceState.LISTENING
	if not await step("voice: both in the call", listening):
		return
	dropped_call = false
	await host()
	var hosting := func() -> bool: return session.local_player != null
	if not await step("follow: the host admits its own player", hosting):
		return
	assert_str(session.local_player.account).is_equal(account_of(own))
	var arrived := func() -> bool:
		return session.players.any(
			func(player: NetwPlayer) -> bool:
				return player.account == account_of(friend),
		)
	if not await step("follow: the friend joins with its account", arrived):
		return
	await seconds(3.0)
	await session.leave().wait()
	await seconds(15.0)
	assert_bool(dropped_call).override_failure_message("carry").is_false()
	assert_array(party.members).is_equal(pair(friend))

	var old_secret := party.join_secret
	party.kick(friend)
	var moved := func() -> bool:
		return party.join_secret != old_secret and party.members == [own] \
				and party.in_call and party.leader == own \
				and social.client.get_calls().size() == 1
	if not await step("kick: a new party with the call moved", moved, 60.0):
		return

	party.invite(friend)
	if not await step("invite: the friend joins the new party", together):
		return
	var alone := func() -> bool: return party.members == [own]
	if not await step("leave: the friend leaves", alone, 60.0):
		return
	party.leave()
	await step("leave: no lobby held", holds_no_lobby, 30.0)


func follow(leader: int) -> void:
	var own := social.user_id
	assert_array(social.friends).contains([leader])
	var host_profile := social.profile(leader)
	var solo := func() -> bool:
		return host_profile.is_playing and host_profile.party_size == 1
	if not await step("ask: the leader shows a party of one", solo, 60.0):
		return
	var endpoints: Array[NetwEndpoint] = []
	session.connection.invited.connect(endpoints.append)

	party.join(leader)
	var together := func() -> bool:
		return party.members == pair(leader) and party.leader == leader
	if not await step("ask: in the leader's party", together):
		return
	var listening := func() -> bool:
		return party.in_call and party.voice_state(leader) \
				== DiscordParty.VoiceState.LISTENING
	if not await step("voice: both in the call", listening):
		return
	dropped_call = false

	var invited := func() -> bool: return not endpoints.is_empty()
	if not await step("follow: the leader's server arrives", invited):
		return
	assert_str(endpoints[0].invited_by).is_equal(host_profile.display_name)
	Netw.prepare_join(gate, &"member")
	var joined := session.connection.join(endpoints[0])
	await joined.wait()
	assert_bool(joined.is_failed).is_false()
	var admitted := func() -> bool: return session.local_player != null
	if not await step("follow: the leader admits the player", admitted):
		return
	assert_str(session.local_player.account).is_equal(account_of(own))
	var started := Time.get_ticks_msec()
	var proof := await social.prove("second-prove")
	print(
		"NETW_DISCORD_LIVE second prove took %.2fs, under 1s is silent"
		% ((Time.get_ticks_msec() - started) / 1000.0),
	)
	assert_bool(proof.is_empty()).is_false()

	var ended := func() -> bool:
		return session.state == NetwSession.STATE_OFFLINE
	if not await step("carry: the leader ends the session", ended):
		return
	await seconds(10.0)
	assert_bool(dropped_call).override_failure_message("carry").is_false()
	assert_array(party.members).is_equal(pair(leader))

	var alone := func() -> bool:
		return party.members == [own] and party.leader == own
	if not await step("kick: back to a party of one", alone, 60.0):
		return
	var offered := func() -> bool: return leader in party.invitations
	if not await step("invite: the leader's invitation arrives", offered):
		return
	party.join(leader)
	if not await step("invite: in the leader's new party", together):
		return
	assert_array(party.invitations).is_empty()

	await seconds(3.0)
	party.leave()
	await step("leave: no lobby held", holds_no_lobby, 30.0)
