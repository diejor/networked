## Integration test for the third-party observer relay.
##
## Verifies the client-facing IMS transition signals:
## [br]- Observers receive [signal NetwInterestLayer.entity_visible].
## [br]- Owners can opt in to [signal NetwEntity.observer_entered].
## [br]- Owner observer reporting stays silent when the flag is off.
class_name TestObserverRelay
extends NetwTestSuite

var harness: NetwTestHarness
var server_scene: NetwSceneHandle
var client0: MultiplayerTree
var client1: MultiplayerTree
var player_builder: PlayerBuilder
var level_builder: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new().with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_interest()
	player_builder.pack()

	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	server_scene = harness.scene_on_server(level_builder.scene_name)


# Spawns client0's player and configures observer awareness on the server.
func _spawn_owner(report: bool) -> Node:
	harness.spawn_player(client0, player_builder.packed)
	var server_player := await harness.wait_for_player(
		harness.server(),
		level_builder.scene_name,
	)
	NetwEntity.of(server_player).interest._set_report_observers(report)
	# Spawn the player on the client too so the relay's path lookup
	# resolves to a live node on the receiving side.
	await harness.wait_for_player(client0, level_builder.scene_name)
	return server_player


func test_relay_fires_on_unbound_layer() -> void:
	var server_player := await _spawn_owner(true)
	var entity := NetwEntity.of(server_player)
	await harness.admit_client_to_scene(client1, level_builder.scene_name)

	var server_tree := harness.server() as MultiplayerTree
	var sight := server_tree.api._interest.layer(&"sight")
	sight.add_entity(entity)

	# Resolve the owner-side entity (on client0) to listen for the
	# relayed signals.
	var owner_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	)
	var owner_entity := NetwEntity.of(owner_player)
	var client1_layer := client1.api._interest.layer(&"sight")

	var entered: Array = []
	var left: Array = []
	var visible: Array = []
	var hidden: Array = []
	owner_entity.interest.on_observed(
		func(peer_id: int):
			entered.append(peer_id)
	)
	owner_entity.interest.on_unobserved(
		func(peer_id: int):
			left.append(peer_id)
	)
	client1_layer.entity_visible.connect(
		func(e: NetwEntity): visible.append(e)
	)
	client1_layer.entity_hidden.connect(
		func(e: NetwEntity): hidden.append(e)
	)

	var client1_peer := client1.multiplayer_peer.get_unique_id()
	monitor_signals(owner_entity, false)
	sight.add_viewer(client1_peer)
	@warning_ignore("redundant_await")
	await assert_signal(owner_entity) \
			.wait_until(1000) \
			.is_emitted("observer_entered", [any(), any()])

	assert_that(entered.size()).is_equal(1)
	assert_that(entered[0]).is_equal(client1_peer)
	assert_that(visible.size()).is_equal(1)
	assert_that(left.is_empty()).is_true()

	sight.remove_viewer(client1_peer)
	@warning_ignore("redundant_await")
	await assert_signal(owner_entity) \
			.wait_until(1000) \
			.is_emitted("observer_left", [any(), any()])

	assert_that(left.size()).is_equal(1)
	assert_that(left[0]).is_equal(client1_peer)
	assert_that(hidden.size()).is_equal(1)


func test_relay_silent_when_flag_off() -> void:
	var server_player := await _spawn_owner(false)
	var entity := NetwEntity.of(server_player)
	await harness.admit_client_to_scene(client1, level_builder.scene_name)

	var server_tree := harness.server() as MultiplayerTree
	var sight := server_tree.api._interest.layer(&"sight")
	sight.add_entity(entity)

	var owner_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	)
	var owner_entity := NetwEntity.of(owner_player)

	var entered: Array = []
	owner_entity.observer_entered.connect(
		func(_l: StringName, _p: int): entered.append(true)
	)

	monitor_signals(owner_entity, false)
	sight.add_viewer(client1.multiplayer_peer.get_unique_id())
	@warning_ignore("redundant_await")
	await assert_signal(owner_entity) \
			.wait_until(300) \
			.is_not_emitted("observer_entered", [any(), any()])


func test_awareness_relay_is_identical_for_scene_layer() -> void:
	var server_player := await _spawn_owner(true)
	var entity := NetwEntity.of(server_player)

	# The scene layer is gated (Node's gate). Use it.
	var scene_layer := server_scene.layer
	scene_layer.add_entity(entity)

	var owner_player := await harness.wait_for_player(
		client0,
		level_builder.scene_name,
	)
	var owner_entity := NetwEntity.of(owner_player)

	var entered: Array = []
	owner_entity.observer_entered.connect(
		func(_l: StringName, _p: int): entered.append(true)
	)

	monitor_signals(owner_entity, false)
	scene_layer.add_viewer(client1.multiplayer_peer.get_unique_id())
	@warning_ignore("redundant_await")
	await assert_signal(owner_entity) \
			.wait_until(1000) \
			.is_emitted("observer_entered", [any(), any()])
	assert_array(entered).contains_exactly([true])
