## Probe and law for ancestry-derived scene membership.
##
## The claim under test is that an entity inside a scene subtree inherits the
## scene's committed row from the interest engine's parent clamp alone, with no
## explicit enrollment in the scene's layer. If that holds, membership is a
## property of the tree rather than a bookkeeping table, and nothing in core has
## to know what a scene is.
##
## The first case is the probe: it reads the observable consequence (does a
## non-admitted peer see the child?) rather than the bookkeeping, so it stays
## true whichever way the mechanism is implemented.
@tool
class_name TestSceneMembershipIsAncestry
extends NetwTestSuite

var harness: NetwTestHarness
var server_api: NetwMultiplayer
var scene: NetwSceneHandle
var client0: MultiplayerTree
var level_builder: LevelBuilder
var probe_builder: PlayerBuilder


func before_test() -> void:
	level_builder = LevelBuilder.new() \
			.with_root(Node2D) \
			.with_multiplayer_spawner()
	level_builder.pack()

	probe_builder = PlayerBuilder.new("AncestryProbe").with_root(Node2D) \
			.with_multiplayer_entity()
	probe_builder.pack()

	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	harness.register_spawnable_scene(level_builder.packed)
	server_api = harness.server().api
	client0 = await harness.add_client()
	await harness.add_clock()
	scene = server_api.scene_instances()[0]


func test_a_child_with_no_membership_inherits_the_scene_row() -> void:
	var child := _spawn_into_scene("Inherits")
	var api := harness.server().api
	var peer_id := client0.multiplayer_peer.get_unique_id()

	# The child declares no membership of its own.
	assert_bool(child.interest._layer_ids.is_empty()).is_true()

	# Not admitted: the parent clamp must hide the child.
	assert_bool(api._interest.participant_sees(peer_id, child)).is_false()

	scene.admit(peer_id)
	api.interest_flush()

	# Admitted: the child rides the scene's row without ever joining its layer.
	assert_bool(api._interest.participant_sees(peer_id, child)).is_true()


func test_the_child_follows_the_scene_it_is_reparented_into() -> void:
	var second_scene := server_api.scene_spawn(level_builder.resource_path)
	await drain_frames(get_tree(), 2)
	var api := harness.server().api
	var peer_id := client0.multiplayer_peer.get_unique_id()
	var child := _spawn_into_scene("Moves")

	# Only the second scene admits the peer, so the child is hidden where it is.
	second_scene.admit(peer_id)
	api.interest_flush()
	assert_bool(api._interest.participant_sees(peer_id, child)).is_false()

	child.owner.get_parent().remove_child(child.owner)
	second_scene.level.add_child(child.owner)
	await drain_frames(get_tree(), 2)
	api.interest_flush()

	# Membership followed the tree, with nothing re-enrolling the child.
	assert_bool(api._interest.participant_sees(peer_id, child)).is_true()



func _spawn_into_scene(node_name: String) -> NetwEntity:
	var node := probe_builder.packed.instantiate()
	node.name = node_name
	var entity := harness.server().api._replication.replicate(node)
	scene.level.add_child(node)
	harness.server().api.interest_flush()
	return entity
