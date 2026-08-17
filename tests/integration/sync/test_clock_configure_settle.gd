## A clock that registers into a session already online, observed with no frame.
##
## [MultiplayerClock] catches the session's entry through
## [signal NetwMultiplayer.session_entered], and a clock added after that signal
## has already fired has to catch up. It cannot catch up on the spot, because
## the rest of its own service entry has not run yet. These cases pin the catch
## up to the session's own settle, with nothing awaited and no frame driven.
##
## A listen server is the peer under test because it is online the moment the
## harness returns it, which is the whole precondition: a session not yet online
## takes the signal path instead and the case would prove nothing.
class_name TestClockConfigureSettle
extends NetwTestSuite

var harness: NetwTestHarness
var host: MultiplayerTree


func before_test() -> void:
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	host = await harness.add_listen_server(
		harness.make_sceneless_payload("clock_host"),
	)
	await drain_frames(get_tree(), 2)


## The registration queues the catch-up rather than running it, and the pump is
## what lands it.
func test_a_late_clock_configures_at_the_pump() -> void:
	assert_object(MultiplayerClock.for_node(host)).override_failure_message(
		"the session must have no clock yet, or the catch-up proves nothing",
	).is_null()

	var clock := _add_clock("LateClock")

	assert_bool(_configure_queued(clock)).is_true()
	assert_object(MultiplayerClock.for_node(host)).is_null()

	host.multiplayer.poll()

	assert_bool(_configure_queued(clock)).is_false()
	assert_object(MultiplayerClock.for_node(host)).is_same(clock)


## Whether the session holds [param clock]'s catch-up for its next settle.
func _configure_queued(clock: MultiplayerClock) -> bool:
	return host.api._native_core.settle_has_key(
		StringName("clock-configure?%d" % clock.get_instance_id()),
	)


func _add_clock(node_name: String) -> MultiplayerClock:
	var clock := MultiplayerClock.new()
	clock.name = node_name
	clock.tickrate = 30
	clock.set_physics_process(false)
	host.add_child(clock)
	return clock
