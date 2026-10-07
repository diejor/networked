class_name TestDiscordRendezvous
extends NetwTestSuite

const _TIMEOUT := 10.0
const _ERROR_SCRUBBER := preload(
	"res://addons/networked_test/gdunit4/gdunit_error_scrubber.gd"
)

var _trees: Array = []
var _services: Array = []
var _instance_id := ""


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func after_test() -> void:
	NakamaWrapper.proxy_base_resolver = Callable()
	await _delete_rendezvous_records()
	for tree in _trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	_trees.clear()
	await super.after_test()


func _delete_rendezvous_records() -> void:
	if _instance_id.is_empty():
		_services.clear()
		return
	for service: DiscordActivityService in _services:
		if not is_instance_valid(service):
			continue
		var session := NakamaSessionService.of(service)
		if session == null:
			continue
		var rdv := service.rendezvous as NakamaDiscordRendezvous
		if rdv == null:
			continue
		var wrapper := NakamaWrapper.new()
		wrapper.use_session(session)
		await wrapper.delete_public_storage(rdv.collection, _instance_id)
	_services.clear()
	_instance_id = ""


func test_two_participants_rendezvous_into_one_match() -> void:
	var instance_id := "disc-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	_instance_id = instance_id

	var host_service := _build_participant("alice", instance_id)
	var host_err: Error = await host_service.connect_activity(&"alice")
	assert_int(host_err).is_equal(OK)

	var join_service := _build_participant("bruno", instance_id)
	var join_err: Error = await join_service.connect_activity(&"bruno")
	assert_int(join_err).is_equal(OK)

	var host: NetwSession = Netw.session(host_service)
	var joined: NetwSession = Netw.session(join_service)

	await _await(
		func() -> bool:
			return _both_connected(host, joined),
		"both Discord participants to connect",
	)

	for session in [host, joined]:
		assert_int(session.root.multiplayer.get_peers().size()).is_equal(1)
		assert_int(session.players.size()).is_equal(2)

	assert_int(host.root.multiplayer.get_unique_id()).is_equal(1)
	assert_int(joined.root.multiplayer.get_unique_id()).is_not_equal(1)


func test_find_answers_null_until_a_host_records_itself() -> void:
	var instance_id := "disc-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	_instance_id = instance_id
	var service := _build_participant("alice", instance_id)
	var rendezvous := service.rendezvous as NakamaDiscordRendezvous
	var connection: NetwConnection = Netw.session(service).connection
	assert_object(await rendezvous.find(instance_id, connection)).is_null()

	var wrapper := await rendezvous.authenticated_wrapper()
	await wrapper.write_public_storage(
		rendezvous.collection,
		instance_id,
		{ "match_id": "recorded.node", "ts": Time.get_unix_time_from_system() },
	)

	var found := await rendezvous.find(instance_id, connection)

	assert_str(found.peer_class).is_equal("NakamaRelayPeer")
	assert_str(found.address).is_equal("recorded.node")


func test_a_record_with_no_match_behind_it_falls_back_to_hosting() -> void:
	var instance_id := "disc-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	_instance_id = instance_id
	var service := _build_participant("alice", instance_id)
	var rendezvous := service.rendezvous as NakamaDiscordRendezvous
	var wrapper := await rendezvous.authenticated_wrapper()
	await wrapper.write_public_storage(
		rendezvous.collection,
		instance_id,
		{ "match_id": "gone.node", "ts": Time.get_unix_time_from_system() },
	)

	var err: Error = await service.connect_activity(&"alice")

	assert_int(err).is_equal(OK)
	assert_int(Netw.session(service).role).is_not_equal(NetwSession.ROLE_CLIENT)
	await _ERROR_SCRUBBER.erase_matching(["_join_lobby did not join a match"])
	await _ERROR_SCRUBBER.erase_matching(["Joining the Nakama match failed"])


func test_claim_hands_the_race_to_the_freshest_host() -> void:
	var instance_id := "disc-%d-%d" % [Time.get_unix_time_from_system(), randi()]
	_instance_id = instance_id
	var first := _build_participant("alice", instance_id)
	var second := _build_participant("bruno", instance_id)
	var connections: Array[NetwConnection] = []
	for service in [first, second]:
		var connection: NetwConnection = Netw.session(service).connection
		var rendezvous := service.rendezvous as NakamaDiscordRendezvous
		await rendezvous.authenticated_wrapper()
		await connection.host(
			rendezvous.host_transport(),
			rendezvous.host_settings(),
		).wait()
		connections.append(connection)
	var first_rendezvous := first.rendezvous as NakamaDiscordRendezvous
	var second_rendezvous := second.rendezvous as NakamaDiscordRendezvous

	var alone := await first_rendezvous.claim(instance_id, connections[0])
	var wrapper := await second_rendezvous.authenticated_wrapper()
	await wrapper.write_public_storage(
		second_rendezvous.collection,
		instance_id,
		{
			"match_id": connections[1].join_address,
			"ts": Time.get_unix_time_from_system() + 60.0,
		},
	)
	var winner := await first_rendezvous.claim(instance_id, connections[0])

	assert_object(alone).is_null()
	assert_str(winner.address).is_equal(connections[1].join_address)


func _build_participant(
		username: String,
		instance_id: String,
) -> DiscordActivityService:
	var tree := MultiplayerViewport.new()
	tree.name = StringName("DiscordTree_%s" % username)

	var session := NakamaSessionService.new()
	session.name = &"NakamaSession"
	session.host = NakamaTestServer.host()
	session.port = NakamaTestServer.DEFAULT_PORT
	session.use_ssl = false
	tree.add_child(session)

	var dir := NakamaLobbyDirectory.new()
	dir.name = &"NakamaLobbyDirectory"
	dir.host = NakamaTestServer.host()
	dir.port = NakamaTestServer.DEFAULT_PORT
	dir.use_ssl = false
	tree.add_child(dir)

	var rdv := NakamaDiscordRendezvous.new()
	rdv.host = NakamaTestServer.host()
	rdv.port = NakamaTestServer.DEFAULT_PORT
	rdv.use_ssl = false

	var service := NetwTestDiscordService.new()
	service.name = &"DiscordActivity"
	service.rendezvous = rdv
	service.fake_instance_id = instance_id
	service.fake_device_id = username
	tree.add_child(service)

	add_child(tree)
	_trees.append(tree)
	_services.append(service)
	return service


func _both_connected(a: NetwSession, b: NetwSession) -> bool:
	return a.is_online and b.is_online \
			and a.root.multiplayer.get_peers().size() == 1 \
			and b.root.multiplayer.get_peers().size() == 1 \
			and a.players.size() == 2 \
			and b.players.size() == 2


func _await(
		cond: Callable,
		label: String,
		timeout: float = _TIMEOUT,
) -> void:
	var deadline := get_tree().create_timer(timeout)
	while deadline.time_left > 0.0:
		if cond.call():
			return
		await get_tree().process_frame
	assert_bool(cond.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()
