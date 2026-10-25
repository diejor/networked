class_name TestNakamaJoinToken
extends NetwTestSuite

const KEY := "res://tests/nakama/join_token/test_only_join_key.pub"
const OTHER_KEY := "res://tests/nakama/join_token/test_only_other_key.pub"
const AUDIENCE := "netw-test"
const PORT := 17350
const WAIT_MS := 15000


class Gate extends Node:
	var requests: Array[NetwJoinRequest] = []


	func admit(request: NetwJoinRequest) -> Error:
		if request.peer_id == 1:
			request.account = ""
			return OK
		requests.append(request)
		return OK


var host: Dictionary
var client: MultiplayerViewport
var host_gate: Gate
var client_gate: Gate
var account: NakamaAccount
var shown := ""


@warning_ignore("unused_parameter")
func before(
		do_skip = OS.get_environment("NAKAMA_JOIN_TOKEN_HOST").is_empty(),
		skip_reason = "Start tests/nakama/join_token/docker-compose.yml and set "
		+ "NAKAMA_JOIN_TOKEN_HOST.",
) -> void:
	pass


func before_test() -> void:
	host_gate = Gate.new()
	host = await EnetTestSupport.start_host(self, host_gate, WAIT_MS / 1000.0)
	client = EnetTestSupport.make_client_tree(self, host.port)
	client.api.inner.auth_timeout = WAIT_MS / 1000.0
	client_gate = Gate.new()
	client.add_child(client_gate)

	account = NakamaAccount.new()
	account.host = OS.get_environment("NAKAMA_JOIN_TOKEN_HOST")
	account.port = PORT
	client.add_child(account)
	var made := await account.client.authenticate_device_async(
		"netw-join-%d-%d" % [Time.get_unix_time_from_system(), randi()],
		null,
		true,
	)
	shown = "Jose %d" % randi()
	await account.client.update_account_async(made, null, shown)
	account.use_session(made)
	await account.sign_in().wait()


func after_test() -> void:
	await EnetTestSupport.stop_tree(client)
	await EnetTestSupport.stop_tree(host.tree)


func join() -> NetwJoinRequest:
	var peer := ENetMultiplayerPeer.new()
	peer.create_client("127.0.0.1", host.port)
	Netw.prepare_join(client_gate, &"player")
	client.api.multiplayer_peer = peer

	var started := Time.get_ticks_msec()
	while client.api.local_player == null \
			and Time.get_ticks_msec() - started < WAIT_MS:
		await get_tree().process_frame
	assert_object(client.api.local_player).is_not_null()
	assert_int(host_gate.requests.size()).is_equal(1)
	return host_gate.requests.back() if not host_gate.requests.is_empty() \
	else null


func test_a_signed_in_player_joins_as_their_nakama_account(
		timeout := WAIT_MS * 2,
) -> void:
	var token := NakamaIdToken.new(KEY, AUDIENCE)
	Netw.configure_admission(host_gate.admit).accept(token)
	Netw.configure_admission(client_gate.admit).accept(token)

	var request := await join()

	var own := "nakama:" + account.user_id
	assert_array(request.accounts).contains_exactly([own])
	assert_str(request.name_of(own)).is_equal(shown)
	assert_str(client.api.local_player.account).is_equal(own)


func test_only_the_first_account_in_the_process_supplies_tokens(
		timeout := WAIT_MS * 2,
) -> void:
	var other_tree: MultiplayerViewport = auto_free(MultiplayerViewport.new())
	add_child(other_tree)
	var other := NakamaAccount.new()
	other.host = account.host
	other.port = PORT
	other_tree.add_child(other)
	other.use_session(
		await other.client.authenticate_device_async(
			"netw-join-other-%d-%d" % [Time.get_unix_time_from_system(), randi()],
			null,
			true,
		),
	)
	await other.sign_in().wait()
	var token := NakamaIdToken.new(KEY, AUDIENCE)
	Netw.configure_admission(host_gate.admit).accept(token)
	Netw.configure_admission(client_gate.admit).accept(token)

	var request := await join()

	assert_array(request.accounts) \
			.override_failure_message(
				"A second NakamaAccount replaced the first one's join tokens.",
			).contains_exactly(["nakama:" + account.user_id])


func test_a_token_another_key_signed_proves_nothing(
		timeout := WAIT_MS * 2,
) -> void:
	Netw.configure_admission(host_gate.admit) \
			.accept(NakamaIdToken.new(OTHER_KEY, AUDIENCE))
	Netw.configure_admission(client_gate.admit) \
			.accept(NakamaIdToken.new(KEY, AUDIENCE))

	var request := await join()

	assert_array(request.accounts).is_empty()
	assert_str(client.api.local_player.account).is_empty()
