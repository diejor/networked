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
	var tree := _make_tree(NetwMultiplayer.Role.CLIENT, "ConnectTargetTree")
	session.adopt_tree(tree, NetwMultiplayer.Role.CLIENT)
	assert_that(tree.scheme).is_equal(&"local")

	var target := session.make_connect_target(tree)
	assert_that(target.scheme).is_equal(&"local")
	assert_that(target.address).is_equal("localhost")

	target = session.make_connect_target(tree, "room-1")
	assert_that(target.scheme).is_equal(&"local")
	assert_that(target.address).is_equal("room-1")

	var payload := session.build_join_payload("valeria")
	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.arg_values).is_empty()

	var source := JoinPayload.new()
	source.username = "ignored"
	source.arg_values = [&"arena", NodePath("Players/Root")]
	payload = session.build_join_payload("valeria", source)
	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.arg_values).is_equal(source.arg_values)


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
		NetwMultiplayer.Role.DEDICATED_SERVER,
		"ConnectHost",
	)
	var err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.OPEN_HOST,
	)
	assert_that(err).is_equal(OK)
	assert_bool(server.api.is_online).is_true()
	assert_that(server.role).is_equal(NetwMultiplayer.Role.DEDICATED_SERVER)

	var client := _make_tree(NetwMultiplayer.Role.CLIENT, "ConnectJoinClient")
	err = await session.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		session.build_join_payload("valeria"),
	)
	assert_that(err).is_equal(OK)
	assert_bool(client.api.is_online).is_true()
	assert_that(client.role).is_equal(NetwMultiplayer.Role.CLIENT)

	await _reset_session()

	var join_or_host := _make_tree(
		NetwMultiplayer.Role.LISTEN_SERVER,
		"ConnectJoinOrHost",
	)
	err = await session.connect_tree(
		join_or_host,
		NetwHarnessSession.Entry.JOIN_OR_HOST,
		session.build_join_payload("host"),
	)
	assert_that(err).is_equal(OK)
	assert_bool(join_or_host.api.is_online).is_true()
	assert_that(join_or_host.role).is_equal(NetwMultiplayer.Role.LISTEN_SERVER)

	await _reset_session()

	var host_player := _make_tree(
		NetwMultiplayer.Role.LISTEN_SERVER,
		"ConnectHostPlayer",
	)
	err = await session.connect_tree(
		host_player,
		NetwHarnessSession.Entry.HOST,
		session.build_join_payload("host"),
	)
	assert_that(err).is_equal(OK)
	assert_bool(host_player.api.is_online).is_true()
	assert_that(host_player.role).is_equal(NetwMultiplayer.Role.LISTEN_SERVER)


func test_disconnect_tree_closes_peer_and_releases_held_packets() -> void:
	var server := _make_tree(
		NetwMultiplayer.Role.DEDICATED_SERVER,
		"DisconnectServer",
	)
	var client := _make_tree(NetwMultiplayer.Role.CLIENT, "DisconnectClient")
	var host_err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.OPEN_HOST,
	)
	assert_that(host_err).is_equal(OK)
	var join_err: Error = await session.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		session.build_join_payload("valeria"),
	)
	assert_that(join_err).is_equal(OK)

	var peer := client.multiplayer_peer as LocalMultiplayerPeer
	var peer_id := peer.get_unique_id()
	var server_peer := server.multiplayer_peer as LocalMultiplayerPeer
	session.session().hold_inbound_packets(peer)
	server_peer.set_target_peer(peer_id)
	server_peer.put_packet(PackedByteArray([1]))
	session.session().poll()
	assert_that(session.session().in_flight_count(peer)).is_equal(1)

	var closed_id := session.disconnect_tree(client)

	assert_that(closed_id).is_equal(peer_id)
	assert_that(client.state).is_equal(NetwMultiplayer.SessionState.DISCONNECTING)
	assert_that(peer.get_unique_id()).is_equal(0)
	assert_that(session.session().is_holding_inbound(peer)).is_false()
	assert_that(session.session().in_flight_count(peer)).is_equal(0)


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
