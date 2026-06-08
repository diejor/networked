class_name TestGameHarnessLinkConditions
extends NetwTestSuite

const _GAME_SCENE := preload(
		"res://tests/support/game_harness_probe_scene.tscn",
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

	game.link(client).exact().delay_polls(4).seed(1)
	assert_that(session.get_link_plan(client_peer)).is_not_null()
	assert_that(session.get_link_plan(client_peer).delay_polls).is_equal(4)

	game.link(host, client).exact().delay_polls(7).seed(2)
	assert_that(
		session.get_link_plan(host_peer, client.peer_id),
	).is_not_null()
	assert_that(
		session.get_link_plan(host_peer, client.peer_id).delay_polls,
	).is_equal(7)
	# The sender keyed condition must not leak into the wildcard slot.
	assert_that(session.get_link_plan(host_peer)).is_null()


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
		session.get_link_plan(client_peer, host.peer_id).delay_polls,
	).is_equal(9)
	assert_that(
		session.get_link_plan(host_peer, client.peer_id).delay_polls,
	).is_equal(9)

	game.degrade(client).inbound().exact().delay_polls(4)
	assert_that(
		session.get_link_plan(client_peer, host.peer_id).delay_polls,
	).is_equal(4)
	assert_that(
		session.get_link_plan(host_peer, client.peer_id).delay_polls,
	).is_equal(9)

	game.path(client, host).exact().delay_polls(7)
	assert_that(
		session.get_link_plan(host_peer, client.peer_id).delay_polls,
	).is_equal(7)


func test_link_delays_inbound_rpc() -> void:
	game = make_game_harness(_GAME_SCENE)
	await game.setup()

	var host := await game.add_host("host", false)
	var client := await game.add_client("valeria", false)
	var host_probe := host.find(_PROBE_PATH) as InboundRpcProbe
	assert_that(host_probe).is_not_null()

	host_probe.apply_value.rpc(10)
	var baseline_ticks := await _ticks_until_value(client, 10, 8)
	assert_that(baseline_ticks).is_greater_equal(0)

	game.link(client).exact().delay_polls(6).seed(20)

	host_probe.apply_value.rpc(20)
	await game.sync_ticks(1)
	var client_peer := client.tree.multiplayer_peer as LocalMultiplayerPeer
	var state = game._loopback.session()._links_by_peer[client_peer]
	assert_that(state.in_flight.size()).is_greater(0)
	var delayed_ticks := await _ticks_until_value(client, 20, 16)

	assert_that(delayed_ticks).is_greater(baseline_ticks)


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
