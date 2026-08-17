## Law suite for the scene machine's two settlement windows under pump drive
## alone: a participant released from a scene loses its membership, and a player
## that leaves its seat for good has that seat released, both with no idle frame
## anywhere in the run.
##
## Each window exists to tell one event from a competing one — a release from a
## reassigning move, a free from a reparent — and neither can answer at the
## signal that opens it. The pump is when they answer, so the rig pumps through
## [method MultiplayerAPI.poll] and never yields a frame.
class_name TestSceneWindowsSettleWithoutFrames
extends NetwTestSuite

const SPAWNER_PATH := "TestPlayer"

var harness: NetwTestHarness
var host: MultiplayerTree
var player_builder: PlayerBuilder
var level: LevelBuilder


func before_test() -> void:
	player_builder = PlayerBuilder.new("TestPlayer") \
			.with_root(Node2D) \
			.with_multiplayer_entity()
	player_builder.pack()

	var template: Node = player_builder.packed.instantiate()
	level = LevelBuilder.new("WindowLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed]) \
			.with_child(template)
	level.pack()
	template.free()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level.packed)
	host = await harness.add_listen_server(
		harness.make_spawn_payload(
			"window_host",
			level.resource_path,
			SPAWNER_PATH,
		),
	)
	await harness.wait_for_player(host, level.scene_name)


func test_a_release_between_pumps_clears_membership_with_no_frame() -> void:
	var scene := _scene()
	assert_that(_participant().current_scene).is_not_null()

	scene.release(_participant())
	_pump(2)

	assert_that(_participant().current_scene).is_null()


func test_a_player_gone_between_pumps_releases_its_seat_with_no_frame() -> void:
	var scene := _scene()
	var peer := host.api.get_unique_id()
	assert_bool(scene.admits(peer)).is_true()

	var player := _player_node()
	player.get_parent().remove_child(player)
	_pump(2)

	assert_bool(scene.admits(peer)).is_false()
	player.free()


func _pump(rounds: int) -> void:
	for _i in rounds:
		host.multiplayer.poll()


func _scene() -> NetwSceneHandle:
	return host.api.scene(level.scene_name)


func _participant() -> NetwParticipant:
	return host.api.local_participant


func _player_node() -> Node:
	return _scene().player_by_peer(host.api.get_unique_id()).owner
