## Regression tests for lag-compensation config lifetime.
##
## Freeing a [LagCompensation] configurator must not stop rewind. Its config,
## its clock binding, and its action channel all target
## [NetwLagCompensationInterface], so a scene change that frees the node leaves
## [method NetwLagCompensationInterface.is_configured] true and the tick loop
## still driving [method NetwLagCompensationInterface.tick_step].
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

	# The clock bind and channel wiring run on session entry, which a bare tree
	# never reaches, so drive them directly.
	sim._on_session_entered()

	var engine := mt.api.lag_compensation
	assert_that(engine.is_configured()).is_true()
	assert_that(mt.api.clock.on_tick.is_connected(engine.tick_step)).is_true()

	mt.remove_child(sim)
	sim.free()

	# Freeing the node used to strip the config and unbind the clock. Now both
	# outlive it.
	assert_that(engine.is_configured()).is_true()
	assert_that(mt.api.clock.on_tick.is_connected(engine.tick_step)).is_true()
