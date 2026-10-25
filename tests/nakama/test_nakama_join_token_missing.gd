class_name TestNakamaJoinTokenMissing
extends NetwTestSuite

const KEY := "res://tests/nakama/join_token/test_only_join_key.pub"
const WAIT_MS := 15000
const _ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)


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


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	host_gate = Gate.new()
	host = await EnetTestSupport.start_host(self, host_gate, WAIT_MS / 1000.0)
	client = EnetTestSupport.make_client_tree(self, host.port)
	client_gate = Gate.new()
	client.add_child(client_gate)


func after_test() -> void:
	await EnetTestSupport.stop_tree(client)
	await EnetTestSupport.stop_tree(host.tree)


func test_a_server_without_the_module_lets_the_player_join_unproven(
		timeout := WAIT_MS * 2,
) -> void:
	var account := NakamaAccount.new()
	account.name = &"NakamaAccount"
	account.host = NakamaTestServer.host()
	account.port = NakamaTestServer.DEFAULT_PORT
	client.add_child(account)
	await NakamaTestSupport.sign_in(client, "join-missing")
	var token := NakamaIdToken.new(KEY)
	Netw.configure_admission(host_gate.admit).accept(token)
	Netw.configure_admission(client_gate.admit).accept(token)

	var peer := ENetMultiplayerPeer.new()
	peer.create_client("127.0.0.1", host.port)
	Netw.prepare_join(client_gate, &"player")
	client.api.multiplayer_peer = peer
	var started := Time.get_ticks_msec()
	while client.api.local_player == null \
			and Time.get_ticks_msec() - started < WAIT_MS:
		await get_tree().process_frame

	assert_object(client.api.local_player) \
			.override_failure_message("The player was not let in.") \
			.is_not_null()
	assert_int(host_gate.requests.size()).is_equal(1)
	if not host_gate.requests.is_empty():
		assert_array(host_gate.requests[0].accounts).is_empty()
	assert_int(
		await _ERROR_SCRUBBER.erase_matching(
			["Install addons/networked_nakama/server"],
		),
	).override_failure_message(
		"The missing module was not reported exactly once.",
	).is_equal(1)
