class_name TestGameHarnessTreelessWrap
extends NetwTestSuite

const TREELESS := preload("res://tests/support/scene/treeless_level.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(TREELESS)
	await game.setup()


# A game scene with no authored MultiplayerTree still yields a live session: the
# harness constructs a tree, wraps the scene, and the host roster forms. This is
# the "pass the game scene directly" on-ramp the harness provides.
func test_treeless_scene_is_wrapped_in_a_constructed_tree() -> void:
	var host := await game.add_host("valeria", false)

	assert_that(host.tree).is_not_null()
	assert_bool(host.tree.is_inside_tree()).is_true()
	assert_that(host.tree.get_parent()).is_equal(host.slot)


# The constructed tree admits a second participant, so the wrap produces one
# independent tree per window exactly like an authored tree would.
func test_wrapped_tree_admits_a_client() -> void:
	var host := await game.add_host("valeria", false)
	var client := await game.add_client("jose", false)

	assert_that(client.tree).is_not_null()
	assert_that(client.tree).is_not_same(host.tree)

	var admitted := false
	for participant: NetwParticipant in host.tree.api.participants:
		if participant.peer_id == client.peer_id:
			admitted = true
			break
	assert_bool(admitted).is_true()
