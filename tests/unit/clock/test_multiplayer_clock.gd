## Unit tests for the [MultiplayerClock] configurator node.
##
## The tick engine's own laws are native, in
## [code]clock_core_tests.cpp[/code]. What is left here is the half that needs
## a tree: the node lookup that resolves a clock from anywhere in a scene.
class_name TestMultiplayerClock
extends NetwTestSuite

func test_for_node_lookup() -> void:
	var node := Node.new()
	add_child(node)
	auto_free(node)
	assert_that(MultiplayerClock.for_node(node)).is_null()

	var api := node.multiplayer
	assert_that(api).is_not_null()

	var clock := MultiplayerClock.new()
	auto_free(clock)
	api.set_meta(&"_multiplayer_clock", clock)

	assert_that(MultiplayerClock.for_node(node)).is_same(clock)

	api.remove_meta(&"_multiplayer_clock")
