## Unit tests for [NetwHarnessSession] shared harness plumbing.
class_name TestNetwHarnessSession
extends NetwTestSuite

var session: NetwHarnessSession
var _trees: Array[MultiplayerTree] = []


func before_test() -> void:
	session = NetwHarnessSession.new()


func after_test() -> void:
	for tree in _trees:
		if is_instance_valid(tree):
			tree.queue_free()
	_trees.clear()
	if get_tree():
		await NetwTestSuite.drain_frames(get_tree(), 2)
	session.reset()
	await super.after_test()


func test_adapter_helper_flow() -> void:
	var tree := _make_tree(NetwMultiplayer.ROLE_CLIENT, "AdapterTree")
	session.adopt_tree(tree, NetwMultiplayer.ROLE_CLIENT)
	assert_that(tree.peer_class).is_equal(&"LocalMultiplayerPeer")

	var adapter := NetwHarnessSession.LoopbackAdapter.new(session.session())
	var server: MultiplayerPeer = adapter.make_peer(
		tree,
		NetwHarnessSession.Entry.HOST,
	)
	assert_object(server).is_instanceof(LocalMultiplayerPeer)
	assert_int(server.get_unique_id()).is_equal(1)

	var client: MultiplayerPeer = adapter.make_peer(
		tree,
		NetwHarnessSession.Entry.JOIN,
	)
	assert_object(client).is_instanceof(LocalMultiplayerPeer)
	assert_int(client.get_unique_id()).is_not_equal(1)

	assert_that(session.build_join_args()).is_empty()

	var spelled: Array = [&"arena", NodePath("Players/Root")]
	assert_that(session.build_join_args(spelled)).is_equal(spelled)


func test_link_conditions_for_sender() -> void:
	var server := session.session().get_server_peer()
	var client := session.session().create_client_peer()
	session.session().poll()

	var conditions := LocalLinkConditions.create(44)
	conditions.latency_ms = 50.0
	session.set_link_conditions(
		server,
		conditions,
		client.get_unique_id(),
	)

	var installed := session.get_link_conditions(
		server,
		client.get_unique_id(),
	)
	assert_that(installed.latency_ms).is_equal(50.0)
	assert_that(installed.effective_latency_ms()).is_equal(50.0)

	session.clear_link_conditions(server, client.get_unique_id())
	assert_that(
		session.get_link_conditions(server, client.get_unique_id()),
	).is_null()


func test_connect_entry_flow() -> void:
	var server := _make_tree(
		NetwMultiplayer.ROLE_DEDICATED_SERVER,
		"ConnectHost",
	)
	var err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.HOST,
	)
	assert_that(err).is_equal(OK)
	assert_bool(server.api.is_online).is_true()
	assert_that(server.api.role).is_equal(NetwMultiplayer.ROLE_DEDICATED_SERVER)
	assert_object(server.api.local_participant).is_null()

	var client := _make_tree(NetwMultiplayer.ROLE_CLIENT, "ConnectJoinClient")
	err = await session.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		&"valeria",
	)
	assert_that(err).is_equal(OK)
	assert_bool(client.api.is_online).is_true()
	assert_that(client.api.role).is_equal(NetwMultiplayer.ROLE_CLIENT)

	await _reset_session()

	var host_player := _make_tree(
		NetwMultiplayer.ROLE_LISTEN_SERVER,
		"ConnectHostPlayer",
	)
	err = await session.connect_tree(
		host_player,
		NetwHarnessSession.Entry.HOST,
		&"host",
	)
	assert_that(err).is_equal(OK)
	assert_bool(host_player.api.is_online).is_true()
	assert_that(host_player.api.role).is_equal(
		NetwMultiplayer.ROLE_LISTEN_SERVER,
	)


func test_disconnect_tree_closes_peer_and_releases_held_packets() -> void:
	var server := _make_tree(
		NetwMultiplayer.ROLE_DEDICATED_SERVER,
		"DisconnectServer",
	)
	var client := _make_tree(NetwMultiplayer.ROLE_CLIENT, "DisconnectClient")
	var host_err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.HOST,
	)
	assert_that(host_err).is_equal(OK)
	var join_err: Error = await session.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		&"valeria",
	)
	assert_that(join_err).is_equal(OK)

	var peer := client.api.multiplayer_peer as LocalMultiplayerPeer
	var peer_id := peer.get_unique_id()
	var server_peer := server.api.multiplayer_peer as LocalMultiplayerPeer
	session.session().hold_inbound_packets(peer)
	server_peer.set_target_peer(peer_id)
	server_peer.put_packet(PackedByteArray([1]))
	session.session().poll()
	assert_that(session.session().in_flight_count(peer)).is_equal(1)

	var closed_id := session.disconnect_tree(client)

	assert_that(closed_id).is_equal(peer_id)
	assert_that(client.api.state).is_equal(
		NetwMultiplayer.SESSION_STATE_DISCONNECTING,
	)
	assert_that(peer.get_unique_id()).is_equal(0)
	assert_that(session.session().is_holding_inbound(peer)).is_false()
	assert_that(session.session().in_flight_count(peer)).is_equal(0)


## The eraser reaches the kit by injection, so nothing anywhere errors if the
## injection is never made and the symptom is a WebRTC suite going red months
## later on a machine where the tolerated error actually fires. These three
## cases are that failure mode's only alarm, and they live here because the
## session hook installs the eraser for every suite in the run.
func test_the_session_hook_installed_the_benign_error_eraser() -> void:
	assert_bool(WebRTCTestSupport.erase_benign_error.is_valid()).is_true()


## The end-to-end proof: an error matching every marker is recorded by the
## framework and then erased, so the case ends clean despite having raised one.
## Without the injection this case fails on the error it planted.
func test_an_error_matching_every_benign_marker_is_erased() -> void:
	push_error("SctpTransport::sendReset failed, errno=2")

	await WebRTCTestSupport.clear_optional_sctp_reset_error()

	assert_bool(true).is_true()


## Erasure is narrow by construction: every marker has to appear before an entry
## is dropped, so widening what the kit tolerates means adding a marker rather
## than loosening a pattern.
func test_the_tolerated_error_is_named_by_every_marker() -> void:
	assert_int(WebRTCTestSupport.SCTP_RESET_MARKERS.size()).is_greater(1)


func _make_tree(
		role: NetwMultiplayer.Role,
		tree_name: String,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = tree_name
	session.adopt_tree(tree, role)
	add_child(tree)
	_trees.append(tree)
	return tree


func _reset_session() -> void:
	for tree in _trees:
		if is_instance_valid(tree):
			tree.queue_free()
	_trees.clear()
	await NetwTestSuite.drain_frames(get_tree(), 2)
	session.reset()
	session = NetwHarnessSession.new()
