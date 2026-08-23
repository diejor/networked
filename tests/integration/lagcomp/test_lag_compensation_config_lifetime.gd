## Regression tests for lag-compensation config lifetime.
##
## Freeing a [LagCompensation] configurator must not stop rewind. Its config,
## its clock binding, and its action channel all target
## [NetwMultiplayer], so a scene change that frees the node leaves
## [method NetwMultiplayer.is_configured] true and the tick loop
## still driving [method NetwMultiplayer.tick_step].
class_name TestLagCompensationConfigLifetime
extends NetwTestSuite

var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func test_config_and_binding_survive_node_free() -> void:
	var clock := MultiplayerClock.new()
	mt.add_child(clock)
	var sim := LagCompensation.new()
	mt.add_child(sim)

	var engine := mt.api
	assert_that(engine.is_configured()).is_true()
	assert_that(mt.api._native_core.on_tick.is_connected(engine.tick_step)).is_true()

	mt.remove_child(sim)
	sim.free()

	# The config and clock binding belong to the engine, not its compiler node.
	assert_that(engine.is_configured()).is_true()
	assert_that(mt.api._native_core.on_tick.is_connected(engine.tick_step)).is_true()
