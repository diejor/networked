## Live end-to-end test of the Discord Activity rendezvous over a Nakama relay.
##
## Two [DiscordActivityService] participants share one fake instance id and carry
## distinct device ids, the same shape a real Activity launch produces. The first
## finds no record and hosts a relay match through its [NakamaDiscordRendezvous];
## the second resolves the freshest record and joins it. Both must end up in one
## match seeing each other.
##
## Needs a running server (see [code]tests/support/nakama/docker-compose.yml[/code])
## and opts in through the [code]networked/tests/nakama_host[/code] project setting
## (see [NakamaTestServer]), so a machine without the stack skips cleanly. This is
## the automated replacement for the old [code]tier1_rendezvous_demo[/code] scene.
class_name TestDiscordRendezvous
extends NetwTestSuite

const _TIMEOUT := 10.0

var _trees: Array = []


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func after_test() -> void:
	# The rendezvous installs a global proxy resolver bound to a per-test
	# rendezvous; drop it so it never leaks into another suite's Nakama connect.
	NakamaWrapper.proxy_base_resolver = Callable()
	for tree in _trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	_trees.clear()
	await super.after_test()


func test_two_participants_rendezvous_into_one_match() -> void:
	# A per-run instance id so the shared rendezvous collection never collides with
	# a leftover record from an earlier run.
	var instance_id := "disc-%d-%d" % [Time.get_unix_time_from_system(), randi()]

	var host_service := _build_participant("alice", instance_id)
	var host_err: Error = await host_service.connect_activity(_payload("alice"))
	assert_int(host_err).is_equal(OK)

	var join_service := _build_participant("bruno", instance_id)
	var join_err: Error = await join_service.connect_activity(_payload("bruno"))
	assert_int(join_err).is_equal(OK)

	var host_tree := MultiplayerTree.resolve(host_service)
	var join_tree := MultiplayerTree.resolve(join_service)

	await _await(
		func() -> bool:
			return _both_connected(host_tree, join_tree),
		"both Discord participants to connect",
	)

	# Two participants in one relay match: one remote peer each, both joined.
	for tree in [host_tree, join_tree]:
		assert_int(tree.multiplayer.get_peers().size()).is_equal(1)
		assert_int(tree.get_joined_players().size()).is_equal(2)

	# The first participant hosts (peer 1); the second resolved the freshest record
	# and joined, so it is never peer 1.
	assert_int(host_tree.multiplayer.get_unique_id()).is_equal(1)
	assert_int(join_tree.multiplayer.get_unique_id()).is_not_equal(1)


# Builds a MultiplayerTree wired exactly like a game embedded in Discord: a shared
# Nakama session, a relay directory, and a DiscordActivityService driven off the
# fake instance-id seam so the rendezvous runs with no SDK and no proxy.
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
	# Distinct device ids keep the two participants distinct Nakama users; the
	# service pushes the id onto the rendezvous when it enters the tree. An empty
	# client_id keeps the connection direct (no discordsays proxy) for localhost.
	service.fake_instance_id = instance_id
	service.fake_device_id = username
	tree.add_child(service)

	add_child(tree)
	_trees.append(tree)
	return service


func _payload(username: String) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = StringName(username)
	return payload


func _both_connected(a: MultiplayerTree, b: MultiplayerTree) -> bool:
	return a.is_online() and b.is_online() \
			and a.multiplayer.get_peers().size() == 1 \
			and b.multiplayer.get_peers().size() == 1


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
