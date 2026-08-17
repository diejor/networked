## Regression tests for clock config lifetime.
##
## Freeing a [MultiplayerClock] configurator must not strip the session's clock
## config. A scene change frees the node under [code]current_scene[/code], so
## the clock has to outlive its node and
## [method ClockCore.is_configured] must stay true afterward.
class_name TestClockConfigLifetime
extends NetwTestSuite

var mt: MultiplayerTree


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)


func _mount_clock() -> MultiplayerClock:
	var clock := MultiplayerClock.new()
	mt.add_child(clock)
	return clock


func test_config_survives_node_free() -> void:
	assert_that(mt.api.clock.is_configured()).is_false()

	var clock := _mount_clock()
	assert_that(mt.api.clock.is_configured()).is_true()

	mt.remove_child(clock)
	clock.free()

	# The freed node stripped its config before the re-home. Now the API keeps it.
	assert_that(mt.api.clock.is_configured()).is_true()


func test_replacement_node_rebinds_after_free() -> void:
	var first := _mount_clock()
	first.tickrate = 20
	mt.api.object_configuration_add(first, first._build_config())
	assert_that(mt.api.clock.tickrate).is_equal(20)

	mt.remove_child(first)
	first.free()
	assert_that(mt.api.clock.is_configured()).is_true()

	var second := _mount_clock()
	second.tickrate = 45
	mt.api.object_configuration_add(second, second._build_config())
	assert_that(mt.api.clock.tickrate).is_equal(45)


func test_poll_pumps_tick_after_node_free() -> void:
	var clock := _mount_clock()
	clock.tickrate = 30
	mt.api.object_configuration_add(clock, clock._build_config())
	mt.remove_child(clock)
	clock.free()

	var engine := mt.api._clock
	# The first poll only seeds the wall-clock baseline.
	engine.poll_step()
	assert_that(engine.tick).is_equal(0)

	# Back-date the baseline so the next poll sees a real frame of elapsed time.
	engine.core.mark_step(0.1)
	engine.poll_step()
	assert_that(engine.tick).is_greater(0)


func test_poll_does_not_pump_while_node_drives() -> void:
	var clock := _mount_clock()
	clock.tickrate = 30
	mt.api.object_configuration_add(clock, clock._build_config())

	var engine := mt.api._clock
	engine.core.mark_step(0.1)
	engine.poll_step()

	# A bound node owns the pump, so the poll leaves the tick alone.
	assert_that(engine.tick).is_equal(0)


func test_stale_exit_does_not_clobber_newer_node() -> void:
	var first := _mount_clock()
	var second := _mount_clock()

	# The second mount is the live endpoint. A late exit from the first must not
	# detach it.
	mt.remove_child(first)
	first.free()

	assert_bool(mt.api.clock.is_configured()).is_true()
	assert_int(mt.api.clock.tickrate).is_equal(second.tickrate)
