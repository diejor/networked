## Integration test for the third-party observer relay.
##
## Verifies the client-facing IMS transition signals:
## [br]- Observers receive [signal NetwInterestLayer.entity_visible].
## [br]- Owners can opt in to [signal NetwEntity.observer_entered].
## [br]- Owner observer reporting stays silent when the flag is off.
class_name TestObserverRelay
extends NetworkedTestSuite

const TEST_LEVEL_SCENE := preload("res://tests/helpers/TestLevel.tscn")
const TEST_PLAYER_SCENE := preload("res://tests/helpers/TestPlayerMinimal.tscn")

var harness: NetworkTestHarness
var server_mgr: MultiplayerSceneManager
var server_scene: MultiplayerScene
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup(NetworkedTestSuite.create_scene_manager)

	server_mgr = harness._get_scene_manager(harness.get_server())
	server_mgr.add_spawnable_scene(TEST_LEVEL_SCENE.resource_path)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	server_scene = server_mgr.active_scenes.values()[0]


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


# Server-side helper: spawn client0's player and inject an
# InterestComponent with [param report] on the server-side copy.
# The flag is server-only; clients don't need their own copy.
# `owner` is set so `%InterestComponent` resolves via the unique-
# name path InterestComponent.of relies on.
func _spawn_owner_with_component(report: bool) -> Node:
	harness.spawn_player(client0, TEST_PLAYER_SCENE)
	var server_player := await harness.wait_for_client_player_spawn(
			harness.get_server(), &"TestLevel")
	var component := InterestComponent.new()
	component.report_observers = report
	server_player.add_child(component)
	component.owner = server_player
	# Spawn the player on the client too so the relay's path lookup
	# resolves to a live node on the receiving side.
	await harness.wait_for_client_player_spawn(client0, &"TestLevel")
	return server_player


func test_relay_fires_on_unbound_layer() -> void:
	var server_player := await _spawn_owner_with_component(true)
	var entity := NetwEntity.of(server_player)
	await harness.admit_client_to_scene(client1, &"TestLevel")

	var server_tree := harness.get_server() as MultiplayerTree
	var sight := server_tree.interest.layer(&"sight")
	sight.add_entity(entity)

	# Resolve the owner-side entity (on client0) to listen for the
	# relayed signals.
	var owner_player := await harness.wait_for_client_player_spawn(
			client0, &"TestLevel")
	var owner_entity := NetwEntity.of(owner_player)
	var client1_layer := client1.interest.layer(&"sight")

	var entered: Array = []
	var left: Array = []
	var visible: Array = []
	var hidden: Array = []
	owner_entity.observer_entered.connect(
			func(layer_id: StringName, peer_id: int):
				entered.append([layer_id, peer_id]))
	owner_entity.observer_left.connect(
			func(layer_id: StringName, peer_id: int):
				left.append([layer_id, peer_id]))
	client1_layer.entity_visible.connect(
			func(e: NetwEntity): visible.append(e))
	client1_layer.entity_hidden.connect(
			func(e: NetwEntity): hidden.append(e))

	var client1_peer := client1.multiplayer_peer.get_unique_id()
	sight.add_viewer(client1_peer)
	await drain_frames(get_tree(), 4)

	assert_that(entered.size()).is_equal(1)
	assert_that(String(entered[0][0])).is_equal("sight")
	assert_that(entered[0][1]).is_equal(client1_peer)
	assert_that(visible.size()).is_equal(1)
	assert_that(left.is_empty()).is_true()

	sight.remove_viewer(client1_peer)
	await drain_frames(get_tree(), 4)

	assert_that(left.size()).is_equal(1)
	assert_that(String(left[0][0])).is_equal("sight")
	assert_that(left[0][1]).is_equal(client1_peer)
	assert_that(hidden.size()).is_equal(1)


func test_relay_silent_when_flag_off() -> void:
	var server_player := await _spawn_owner_with_component(false)
	var entity := NetwEntity.of(server_player)
	await harness.admit_client_to_scene(client1, &"TestLevel")

	var server_tree := harness.get_server() as MultiplayerTree
	var sight := server_tree.interest.layer(&"sight")
	sight.add_entity(entity)

	var owner_player := await harness.wait_for_client_player_spawn(
			client0, &"TestLevel")
	var owner_entity := NetwEntity.of(owner_player)

	var entered: Array = []
	owner_entity.observer_entered.connect(
			func(_l: StringName, _p: int): entered.append(true))

	sight.add_viewer(client1.multiplayer_peer.get_unique_id())
	await drain_frames(get_tree(), 4)

	assert_that(entered.is_empty()).is_true()


func test_relay_skipped_for_gated_layer() -> void:
	# Gated layers replicate viewers via the gate's synced properties,
	# so the owner can already enumerate observers locally. The relay
	# must skip these to avoid double-counting.
	var server_player := await _spawn_owner_with_component(true)
	var entity := NetwEntity.of(server_player)

	# The scene layer is gated (MultiplayerScene's gate). Use it.
	var scene_layer := server_scene.layer
	scene_layer.add_entity(entity)

	var owner_player := await harness.wait_for_client_player_spawn(
			client0, &"TestLevel")
	var owner_entity := NetwEntity.of(owner_player)

	var entered: Array = []
	owner_entity.observer_entered.connect(
			func(_l: StringName, _p: int): entered.append(true))

	scene_layer.add_viewer(client1.multiplayer_peer.get_unique_id())
	await drain_frames(get_tree(), 4)

	assert_that(entered.is_empty()).is_true()
