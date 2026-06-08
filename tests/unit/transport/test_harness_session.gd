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


func test_make_backend_uses_owned_session() -> void:
	var backend := session.make_backend()

	assert_that(backend.session).is_same(session.session())


func test_loopback_adapter_builds_join_target_from_tree_backend() -> void:
	var tree := _make_tree(MultiplayerTree.Role.CLIENT, "JoinTargetTree")
	var target := session.make_join_target(tree)

	assert_that(target.backend).is_same(tree.backend)
	assert_that(target.address).is_equal("localhost")


func test_loopback_adapter_accepts_join_target_address() -> void:
	var tree := _make_tree(MultiplayerTree.Role.CLIENT, "JoinAddressTree")
	var target := session.make_join_target(tree, "room-1")

	assert_that(target.backend).is_same(tree.backend)
	assert_that(target.address).is_equal("room-1")


func test_build_join_payload_without_spawn() -> void:
	var payload := session.build_join_payload("valeria")

	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.spawn).is_empty()


func test_build_join_payload_uses_dictionary_spawn() -> void:
	var spawn := { &"scene": "arena", &"node": "Spawner" }
	var payload := session.build_join_payload("valeria", spawn)

	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.spawn).is_equal(spawn)


func test_build_join_payload_uses_join_payload_spawn() -> void:
	var source := JoinPayload.new()
	source.username = "ignored"
	source.spawn = { &"scene": "arena" }

	var payload := session.build_join_payload("valeria", source)

	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.spawn).is_equal(source.spawn)


func test_build_join_payload_uses_scene_node_path_spawn() -> void:
	var path := SceneNodePath.new()
	path.scene_path = "res://levels/Arena.tscn"
	path.node_path = "Player/MultiplayerEntity"

	var payload := session.build_join_payload("valeria", path)

	assert_that(payload.username).is_equal("valeria")
	assert_that(payload.spawn).is_not_empty()


func test_set_link_conditions_for_sender() -> void:
	var server := session.session().get_server_peer()
	var client := session.session().create_client_peer()
	session.session().poll()

	var conditions := NetwLinkConditions.new(44)
	conditions.delay_polls = 3
	session.set_link_conditions(
		server,
		conditions,
		client._get_unique_id(),
	)

	var installed := session.get_link_conditions(
		server,
		client._get_unique_id(),
	)
	assert_that(installed.delay_polls).is_equal(3)

	session.clear_link_conditions(server, client._get_unique_id())
	assert_that(
		session.get_link_conditions(server, client._get_unique_id()),
	).is_null()


func test_connect_tree_hosts_dedicated_server() -> void:
	var server := _make_tree(
		MultiplayerTree.Role.DEDICATED_SERVER,
		"ConnectHost",
	)

	var err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.HOST,
	)

	assert_that(err).is_equal(OK)
	assert_bool(server.is_online()).is_true()
	assert_that(server.role).is_equal(MultiplayerTree.Role.DEDICATED_SERVER)


func test_connect_tree_joins_existing_host() -> void:
	var server := _make_tree(
		MultiplayerTree.Role.DEDICATED_SERVER,
		"ConnectJoinServer",
	)
	var client := _make_tree(MultiplayerTree.Role.CLIENT, "ConnectJoinClient")
	var host_err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.HOST,
	)
	assert_that(host_err).is_equal(OK)

	var err: Error = await session.connect_tree(
		client,
		NetwHarnessSession.Entry.JOIN,
		session.build_join_payload("valeria"),
	)

	assert_that(err).is_equal(OK)
	assert_bool(client.is_online()).is_true()
	assert_that(client.role).is_equal(MultiplayerTree.Role.CLIENT)


func test_connect_tree_join_or_host_hosts_without_listener() -> void:
	var tree := _make_tree(
		MultiplayerTree.Role.LISTEN_SERVER,
		"ConnectJoinOrHost",
	)

	var err: Error = await session.connect_tree(
		tree,
		NetwHarnessSession.Entry.JOIN_OR_HOST,
		session.build_join_payload("host"),
	)

	assert_that(err).is_equal(OK)
	assert_bool(tree.is_online()).is_true()
	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)


func test_connect_tree_host_player_hosts_local_player() -> void:
	var tree := _make_tree(
		MultiplayerTree.Role.LISTEN_SERVER,
		"ConnectHostPlayer",
	)

	var err: Error = await session.connect_tree(
		tree,
		NetwHarnessSession.Entry.HOST_PLAYER,
		session.build_join_payload("host"),
	)

	assert_that(err).is_equal(OK)
	assert_bool(tree.is_online()).is_true()
	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)


func test_disconnect_tree_closes_peer_and_releases_held_packets() -> void:
	var server := _make_tree(
		MultiplayerTree.Role.DEDICATED_SERVER,
		"DisconnectServer",
	)
	var client := _make_tree(MultiplayerTree.Role.CLIENT, "DisconnectClient")
	var host_err: Error = await session.connect_tree(
		server,
		NetwHarnessSession.Entry.HOST,
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
	server_peer._set_target_peer(peer_id)
	server_peer._put_packet_script(PackedByteArray([1]))
	session.session().poll()
	assert_that(session.session()._links_by_peer.has(peer)).is_true()

	var closed_id := session.disconnect_tree(client)

	assert_that(closed_id).is_equal(peer_id)
	assert_that(client.state).is_equal(MultiplayerTree.State.DISCONNECTING)
	assert_that(peer.get_unique_id()).is_equal(0)
	assert_that(session.session()._held_peers.has(peer)).is_false()
	assert_that(session.session()._links_by_peer.has(peer)).is_false()


func _make_tree(
		role: MultiplayerTree.Role,
		tree_name: String,
) -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = tree_name
	session.adopt_tree(tree, role)
	add_child(tree)
	_trees.append(tree)
	return tree
