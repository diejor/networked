extends NetwTestSuite

const MAIN := preload("res://examples/playground/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func test_a_host_and_a_client_share_the_playground() -> void:
	var host := await game.add_host("hana")
	var ada := await game.add_client("ada")
	await game.sync_ticks(30)

	for peer: NetwSceneRunner in [host, ada]:
		var root := playground(peer)
		assert_int(root.get_node(^"Cubes").get_child_count()).is_equal(900)
		assert_int(root.get_node(^"Players").get_child_count()).is_equal(2)
		assert_bool((peer.local_player as PlayPlayer).entity.is_controlled_locally) \
				.is_true()


func playground(peer: NetwSceneRunner) -> Node:
	return Netw.scene(peer.tree, &"Playground").root
