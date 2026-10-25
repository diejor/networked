class_name TestNakamaParty
extends NetwTestSuite

const PORT := 17350
const TIMEOUT := 15.0

var trees: Array[MultiplayerViewport] = []
var invites: Dictionary[MultiplayerViewport, Array] = { }


@warning_ignore("unused_parameter")
func before(
		do_skip = OS.get_environment("NAKAMA_JOIN_TOKEN_HOST").is_empty(),
		skip_reason = "Start tests/nakama/join_token/docker-compose.yml and set "
		+ "NAKAMA_JOIN_TOKEN_HOST.",
) -> void:
	pass


func after_test() -> void:
	for tree in trees:
		await NakamaTestSupport.stop_tree(tree)
	trees.clear()
	invites.clear()
	await super.after_test()


func player(username: String) -> MultiplayerViewport:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	var account := NakamaTestSupport.account(tree)
	account.host = OS.get_environment("NAKAMA_JOIN_TOKEN_HOST")
	account.port = PORT
	trees.append(tree)
	invites[tree] = []
	Netw.session(tree).connection.invited.connect(
		func(endpoint: NetwEndpoint) -> void: invites[tree].append(endpoint),
	)
	assert_int(await NakamaTestSupport.sign_in(tree, username)).is_equal(OK)
	return tree


func party_of(tree: MultiplayerViewport) -> NakamaParty:
	return NakamaTestSupport.account(tree).party


func id_of(tree: MultiplayerViewport) -> String:
	return NakamaTestSupport.account(tree).user_id


func settle(condition: Callable, label: String) -> void:
	var deadline := get_tree().create_timer(TIMEOUT)
	while not condition.call() and deadline.time_left > 0.0:
		await get_tree().process_frame
	assert_bool(condition.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()


func partied(leader: MultiplayerViewport, friend: MultiplayerViewport) -> void:
	var invited_by: Array[String] = []
	party_of(friend).invited.connect(
		func(from: String) -> void: invited_by.append(from),
	)
	party_of(leader).invite(id_of(friend))
	await settle(func() -> bool: return not invited_by.is_empty(), "the invitation")
	assert_array(invited_by).contains_exactly([id_of(leader)])
	party_of(friend).join(id_of(leader))
	await settle(
		func() -> bool:
			return party_of(friend).members.size() == 2 \
					and party_of(leader).members.size() == 2,
		"the friend to join",
	)


func test_an_invited_friend_joins_the_party() -> void:
	var valeria := await player("valeria")
	var jose := await player("jose")
	assert_array(party_of(valeria).members).contains_exactly([id_of(valeria)])

	await partied(valeria, jose)

	assert_array(party_of(jose).members) \
			.contains_exactly_in_any_order([id_of(valeria), id_of(jose)])
	assert_str(party_of(jose).leader).is_equal(id_of(valeria))
	assert_array(party_of(jose).invitations).is_empty()


func test_a_join_request_is_answered_by_an_invitation() -> void:
	var valeria := await player("valeria")
	var mia := await player("mia")
	var asked: Array[String] = []
	party_of(valeria).join_requested.connect(
		func(friend: String) -> void: asked.append(friend),
	)

	party_of(mia).join(id_of(valeria))
	await settle(func() -> bool: return not asked.is_empty(), "the join request")
	assert_array(party_of(valeria).join_requests).contains_exactly([id_of(mia)])
	party_of(valeria).invite(id_of(mia))

	await settle(
		func() -> bool: return party_of(mia).members.size() == 2,
		"the asking friend to join",
	)
	assert_array(party_of(valeria).join_requests).is_empty()


func test_members_follow_the_leader_into_a_relay_match() -> void:
	var valeria := await player("valeria")
	var jose := await player("jose")
	await partied(valeria, jose)

	var hosted := await NetwOrdinarySetup.connect_through(
		valeria,
		NakamaTestSupport.PEER_CLASS,
		NetwConnection.TRANSPORT_MODE_HOST,
		"",
		{ },
		&"valeria",
	)
	assert_int(hosted.error).is_equal(OK)
	await settle(func() -> bool: return not invites[jose].is_empty(), "the follow")
	var endpoint: NetwEndpoint = invites[jose][0]
	assert_str(endpoint.address) \
			.is_equal(Netw.session(valeria).connection.join_address)
	assert_bool(endpoint.invited_by.is_empty()).is_false()

	var joined := await NetwOrdinarySetup.connect_through(
		jose,
		endpoint.peer_class,
		NetwConnection.TRANSPORT_MODE_CLIENT,
		endpoint.address,
		{ },
		&"jose",
	)
	assert_int(joined.error).is_equal(OK)
	await settle(
		func() -> bool: return Netw.session(valeria).players.size() == 2,
		"both players in the leader's match",
	)


func test_a_kicked_member_is_left_alone() -> void:
	var valeria := await player("valeria")
	var jose := await player("jose")
	await partied(valeria, jose)

	party_of(valeria).kick(id_of(jose))

	await settle(
		func() -> bool:
			return party_of(jose).members == [id_of(jose)] \
					and party_of(valeria).members == [id_of(valeria)],
		"the kick",
	)
	assert_str(party_of(jose).leader).is_equal(id_of(jose))


func test_a_member_who_leaves_is_gone_for_the_leader() -> void:
	var valeria := await player("valeria")
	var jose := await player("jose")
	await partied(valeria, jose)

	party_of(jose).leave()

	await settle(
		func() -> bool: return party_of(valeria).members == [id_of(valeria)],
		"the leave",
	)
	assert_array(party_of(jose).members).contains_exactly([id_of(jose)])


func test_find_match_brings_the_whole_party_into_one_match() -> void:
	var valeria := await player("valeria")
	var jose := await player("jose")
	var mia := await player("mia")
	await partied(valeria, jose)

	party_of(valeria).find_match(3, 3)
	party_of(mia).find_match(3, 3)

	await settle(
		func() -> bool:
			return not invites[valeria].is_empty() \
					and not invites[jose].is_empty() \
					and not invites[mia].is_empty(),
		"the matched invitations",
	)
	var outcomes: Array[NetwOrdinarySetup.Outcome] = []
	for tree: MultiplayerViewport in [valeria, jose, mia]:
		var endpoint: NetwEndpoint = invites[tree].back()
		(func() -> void:
			outcomes.append(
				await NetwOrdinarySetup.connect_through(
					tree,
					endpoint.peer_class,
					NetwConnection.TRANSPORT_MODE_CLIENT,
					endpoint.address,
					{ },
					StringName(tree.name),
				),
			)
		).call()
	await settle(func() -> bool: return outcomes.size() == 3, "every join")
	for outcome in outcomes:
		assert_int(outcome.error).is_equal(OK)
	await settle(
		func() -> bool:
			return Netw.session(valeria).players.size() == 3 \
					and Netw.session(jose).players.size() == 3 \
					and Netw.session(mia).players.size() == 3,
		"every player in the matched match",
	)
