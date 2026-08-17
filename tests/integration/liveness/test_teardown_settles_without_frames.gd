## Law suite for what a session leaves behind when it ends under pump drive
## alone: the per-session registries a peer keeps are empty by the pump that saw
## the session end, with no idle frame anywhere in the run.
##
## A session that ends is exactly when a frame is least promised, because the
## game may be tearing its own tree down in the same breath. The rig pumps
## through [method MultiplayerAPI.poll] and never yields a frame, so a clear
## that waits for one never happens at all.
##
## The peer under test is a client, because a client is the peer that holds a
## whole session's worth of received state to drop.
class_name TestTeardownSettlesWithoutFrames
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayer"

var harness: NetwTestHarness
var client0: MultiplayerTree
var player_builder: PlayerBuilder
var level: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("TestPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	var template: Node = player_builder.packed.instantiate()
	level = LevelBuilder.new("TeardownLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template)
	level.pack()
	template.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level.packed)
	client0 = await harness.add_client()
	await harness.join_player(client0, level.resource_path, SPAWNER_PATH)
	await harness.wait_for_player(client0, level.scene_name)


func test_a_session_ended_between_pumps_clears_the_route_registry() -> void:
	var route := _player_route()
	assert_int(_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)

	client0.multiplayer_peer.close()
	_pump(4)

	assert_int(_state(route)).is_equal(NetwMultiplayer.EntityState.UNKNOWN)


func test_a_session_ended_between_pumps_gives_back_the_spawners() -> void:
	var compat := client0.api._replication._spawner_compat
	assert_int(compat._roster.size()).is_greater(0)

	client0.multiplayer_peer.close()
	_pump(4)

	assert_int(compat._roster.size()).is_equal(0)


func _pump(rounds: int) -> void:
	for _i in rounds:
		client0.multiplayer.poll()


func _state(route: int) -> NetwMultiplayer.EntityState:
	return client0.api.entity_get_state(client0.api.entity_from_route(route))


func _player_route() -> int:
	var scene := client0.api.scene(level.scene_name)
	return scene.player_by_peer(client0.api.get_unique_id()).route
