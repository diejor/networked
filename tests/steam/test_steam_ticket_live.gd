extends GdUnitTestSuite

const WAIT_MS := 30000
const STRANGER := 76561197960287930


class Gate extends Node:
	var requests: Array[NetwJoinRequest] = []


	func admit(request: NetwJoinRequest) -> Error:
		if request.peer_id == 1:
			request.account = ""
			return OK
		requests.append(request)
		return OK


class ClaimsStranger extends SteamTicket:
	func _prove(challenge: String, session: NetwSession) -> Variant:
		var proof: Dictionary = await super(challenge, session)
		proof.steam_id = STRANGER
		return proof


var host: Dictionary
var client: MultiplayerViewport
var host_gate: Gate
var client_gate: Gate


func before(
		do_skip = OS.get_environment("NETW_STEAM_TEST") != "1",
		skip_reason = "Set NETW_STEAM_TEST=1 with a logged-in Steam client.",
) -> void:
	pass


func before_test() -> void:
	host_gate = Gate.new()
	host = await EnetTestSupport.start_host(self, host_gate, WAIT_MS / 1000.0)
	Netw.configure_admission(host_gate.admit).accept(SteamTicket.new())
	client = EnetTestSupport.make_client_tree(self, host.port)
	client.api.inner.auth_timeout = WAIT_MS / 1000.0
	client_gate = Gate.new()
	client.add_child(client_gate)


func after_test() -> void:
	await EnetTestSupport.stop_tree(client)
	await EnetTestSupport.stop_tree(host.tree)


func join() -> NetwJoinRequest:
	var asked := host_gate.requests.size()
	var peer := ENetMultiplayerPeer.new()
	peer.create_client("127.0.0.1", host.port)
	Netw.prepare_join(client_gate, &"player")
	client.api.multiplayer_peer = peer

	var started := Time.get_ticks_msec()
	while client.api.local_player == null \
			and Time.get_ticks_msec() - started < WAIT_MS:
		await get_tree().process_frame
	assert_object(client.api.local_player).is_not_null()
	assert_int(host_gate.requests.size()).is_equal(asked + 1)
	return host_gate.requests.back()


func test_a_ticket_proves_the_steam_account_over_enet_on_every_join(
		timeout := WAIT_MS * 3,
) -> void:
	Netw.configure_admission(client_gate.admit).accept(SteamTicket.new())

	var request := await join()

	var steam := Engine.get_singleton(&"Steam")
	var own := "steam:%d" % steam.getSteamID()
	assert_array(request.accounts).contains_exactly([own])
	assert_str(request.name_of(own)).is_equal(steam.getPersonaName())
	assert_bool(request.is_bound(own)).is_false()
	assert_object(request.session).is_same(Netw.session(host.tree))
	assert_str(client.api.local_player.account).is_equal(own)

	await Netw.session(client).leave().wait()
	var again := await join()

	assert_array(again.accounts).contains_exactly([own])


func test_a_ticket_claimed_for_another_steam_id_proves_nothing(
		timeout := WAIT_MS * 2,
) -> void:
	Netw.configure_admission(client_gate.admit).accept(ClaimsStranger.new())

	var request := await join()

	assert_array(request.accounts).is_empty()
	assert_str(client.api.local_player.account).is_empty()
