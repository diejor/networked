class_name TestDiscordRendezvous
extends NetwTestSuite

const _TIMEOUT := 10.0

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

	var host_tree := Netw.of(host_service).root as MultiplayerTree
	var join_tree := Netw.of(join_service).root as MultiplayerTree

	await _await(
		func() -> bool:
			return _both_connected(host_tree, join_tree),
		"both Discord participants to connect",
	)

	for tree in [host_tree, join_tree]:
		assert_int(tree.multiplayer.get_peers().size()).is_equal(1)
		assert_int(tree.api.players.size()).is_equal(2)

	assert_int(host_tree.multiplayer.get_unique_id()).is_equal(1)
	assert_int(join_tree.multiplayer.get_unique_id()).is_not_equal(1)


func _build_participant(
		username: String,
		instance_id: String,
) -> DiscordActivityService:
	var tree := MultiplayerTree.new()
	tree.name = StringName("DiscordTree_%s" % username)
	tree.auto_host_headless = false

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


func _both_connected(a: MultiplayerTree, b: MultiplayerTree) -> bool:
	return a.api.is_online and b.api.is_online \
			and a.multiplayer.get_peers().size() == 1 \
			and b.multiplayer.get_peers().size() == 1 \
			and a.api.players.size() == 2 \
			and b.api.players.size() == 2


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
