class_name TestGameHarnessLinkConditions
extends NetwTestSuite

const _GAME_SCENE := preload(
	"res://tests/support/game_harness_probe_scene.tscn"
)
const _PROBE_PATH := NodePath("MultiplayerTree/InboundRpcProbe")

var game: NetwGameHarness


func after_test() -> void:
	if is_instance_valid(game):
		await game.teardown()
	game = null
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func test_link_routes_to_correct_peer() -> void:
	game = make_game_harness(_GAME_SCENE)
	await game.setup()

	var host := await game.add_host("host", false)
	var client := await game.add_client("valeria", false)
	var session := game._loopback
	var host_peer := host.tree.multiplayer_peer as LocalMultiplayerPeer
	var client_peer := client.tree.multiplayer_peer as LocalMultiplayerPeer

	game.link(client).latency_ms(40.0).seed(1)
	assert_that(session.get_link_conditions(client_peer)).is_not_null()
	assert_that(session.get_link_conditions(client_peer).latency_ms).is_equal(40.0)

	game.link(host, client).latency_ms(70.0).seed(2)
	assert_that(
		session.get_link_conditions(host_peer, client.peer_id),
	).is_not_null()
	assert_that(
		session.get_link_conditions(host_peer, client.peer_id).latency_ms,
	).is_equal(70.0)
	# The sender keyed condition must not leak into the wildcard slot.
	assert_that(session.get_link_conditions(host_peer)).is_null()


func test_degrade_and_path_read_in_data_flow_order() -> void:
	game = make_game_harness(_GAME_SCENE)
	await game.setup()

	var host := await game.add_host("host", false)
	var client := await game.add_client("valeria", false)
	var session := game._loopback
	var host_peer := host.tree.multiplayer_peer as LocalMultiplayerPeer
	var client_peer := client.tree.multiplayer_peer as LocalMultiplayerPeer

	game.degrade(client).latency_ms(150.0)
	assert_that(
		session.get_link_conditions(client_peer, host.peer_id).latency_ms,
	).is_equal(150.0)
	assert_that(
		session.get_link_conditions(host_peer, client.peer_id).latency_ms,
	).is_equal(150.0)

	game.degrade(client).inbound().latency_ms(40.0)
	assert_that(
		session.get_link_conditions(client_peer, host.peer_id).latency_ms,
	).is_equal(40.0)
	assert_that(
		session.get_link_conditions(host_peer, client.peer_id).latency_ms,
	).is_equal(150.0)

	game.path(client, host).latency_ms(70.0)
	assert_that(
		session.get_link_conditions(host_peer, client.peer_id).latency_ms,
	).is_equal(70.0)


func test_link_delays_inbound_rpc() -> void:
	game = make_game_harness(_GAME_SCENE)
	await game.setup()

	var host := await game.add_host("host", false)
	var client := await game.add_client("valeria", false)
	var host_probe := host.find(_PROBE_PATH) as InboundRpcProbe
	assert_that(host_probe).is_not_null()

	# An undelayed inbound RPC lands on the client.
	host_probe.apply_value.rpc(10)
	assert_that(await _ticks_until_value(client, 10, 8)).is_greater_equal(0)

	# A delay far longer than the test window holds the next RPC in flight. The
	# magnitude keeps the assertion independent of how much loopback time one
	# network tick spans, which shifts with the harness physics rate.
	game.link(client).latency_ms(1_000_000.0).seed(20)
	host_probe.apply_value.rpc(20)
	var client_peer := client.tree.multiplayer_peer as LocalMultiplayerPeer
	var held := await _wait_for_in_flight(client_peer, 8)

	# The packet sits in flight and the client keeps the old value: the link
	# delayed the inbound RPC rather than dropping or applying it.
	assert_that(held).is_true()
	assert_that((client.find(_PROBE_PATH) as InboundRpcProbe).value).is_equal(10)

	# Clearing the link flushes the held packet, so the update finally arrives.
	game.link(client).clear()
	assert_that(await _ticks_until_value(client, 20, 8)).is_greater_equal(0)


func _wait_for_in_flight(peer: LocalMultiplayerPeer, max_ticks: int) -> bool:
	var session := game._loopback.session()
	for tick in range(max_ticks + 1):
		if session._links_by_peer.has(peer) \
				and session._links_by_peer[peer].in_flight.size() > 0:
			return true
		await game.sync_ticks(1)
	return false


func _ticks_until_value(
		runner: NetwSceneRunner,
		expected: int,
		max_ticks: int,
) -> int:
	for tick in range(max_ticks + 1):
		var probe := runner.find(_PROBE_PATH) as InboundRpcProbe
		if probe and probe.value == expected:
			return tick
		await game.sync_ticks(1)
	return -1
