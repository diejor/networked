## Unit tests for [method MultiplayerTree.adopt_peer].
##
## Verifies the externally-produced-peer adoption path used by lobby
## providers, without going through a [BackendPeer].
class_name TestAdoptPeer
extends NetworkedTestSuite


func _make_tree() -> MultiplayerTree:
	var tree := MultiplayerTree.new()
	tree.name = "Tree"
	tree.auto_host_headless = false
	add_child(tree)
	auto_free(tree)
	return tree


func _make_connected_server_peer() -> LocalMultiplayerPeer:
	var peer: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	peer.create_server()
	return peer


func _make_connected_client_peer() -> LocalMultiplayerPeer:
	var server: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	var client: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	server.create_server()
	client.create_client(42)
	server.force_connect_peer(42, client)
	client.force_connect_peer(1, server)
	client.poll()
	return client


func test_adopt_server_peer_sets_listen_server_role() -> void:
	var tree := _make_tree()
	var peer := _make_connected_server_peer()

	var err := await tree.adopt_peer(peer)

	assert_that(err).is_equal(OK)
	assert_that(tree.role).is_equal(MultiplayerTree.Role.LISTEN_SERVER)
	assert_that(tree.state).is_equal(MultiplayerTree.State.ONLINE)
	assert_that(tree.is_online()).is_true()


func test_adopt_client_peer_sets_client_role() -> void:
	var tree := _make_tree()
	var peer := _make_connected_client_peer()

	var err := await tree.adopt_peer(peer)

	assert_that(err).is_equal(OK)
	assert_that(tree.role).is_equal(MultiplayerTree.Role.CLIENT)
	assert_that(tree.state).is_equal(MultiplayerTree.State.ONLINE)


func test_adopt_null_peer_returns_invalid_parameter() -> void:
	var tree := _make_tree()

	var err := await tree.adopt_peer(null)

	assert_that(err).is_equal(ERR_INVALID_PARAMETER)
	assert_that(tree.state).is_equal(MultiplayerTree.State.OFFLINE)
	assert_that(tree.role).is_equal(MultiplayerTree.Role.NONE)


func test_adopt_disconnected_peer_returns_invalid_parameter() -> void:
	var tree := _make_tree()
	var peer: LocalMultiplayerPeer = auto_free(LocalMultiplayerPeer.new())
	# Fresh peer with no create_server/create_client is DISCONNECTED.

	var err := await tree.adopt_peer(peer)

	assert_that(err).is_equal(ERR_INVALID_PARAMETER)
	assert_that(tree.state).is_equal(MultiplayerTree.State.OFFLINE)


func test_adopt_peer_emits_configured_signal() -> void:
	var tree := _make_tree()
	var peer := _make_connected_server_peer()
	var emitted := [false]
	tree.configured.connect(func() -> void: emitted[0] = true)

	tree.adopt_peer(peer)

	assert_that(emitted[0]).is_true()


func test_adopt_peer_assigns_peer_onto_api() -> void:
	var tree := _make_tree()
	var peer := _make_connected_server_peer()

	tree.adopt_peer(peer)

	assert_that(tree.multiplayer_peer).is_equal(peer)
