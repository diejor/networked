## Laws for the clock's simulation gate, the decision that a frame advances the
## simulated world.
##
## A tick is a fixed amount of simulated time. The physics server runs exactly
## one step per frame and takes no argument about how long that step is, so the
## only way a clock can make a tick worth the same amount of time on two peers is
## to decide which frames the world may step at all. A gated clock therefore pays
## each tick into a budget of whole steps and spends one per simulated frame,
## and a frame with nothing to spend holds.
class_name TestClockSimulationGate
extends NetwTestSuite


func _clock(rate: int = 60) -> NetwClockInterface:
	var clock := NetwClockInterface.new()
	clock.tickrate = rate
	clock._configured = true
	return clock


# Frames a gated clock admits while [param ticks_per_frame] names the ticks each
# frame emitted, so an arm can describe a throttled clock as the pattern it
# actually produces.
func _admitted(clock: NetwClockInterface, ticks_per_frame: Array) -> Array[bool]:
	var out: Array[bool] = []
	for ticks: int in ticks_per_frame:
		clock.force_step(ticks)
		out.append(clock.is_simulating)
	return out


func test_an_ungated_clock_simulates_every_frame() -> void:
	var clock := _clock()

	assert_array(_admitted(clock, [1, 0, 1, 0, 0, 1])) \
			.override_failure_message(
				"a game that predicts no engine-integrated body must never see "
				+ "a held frame",
			).is_equal([true, true, true, true, true, true])


# The defect, stated as a law. A throttled clock emits fewer ticks than its
# physics runs frames, and every frame it does not tick is a frame the world
# must not advance either.
func test_a_gated_clock_holds_the_frames_that_run_no_tick() -> void:
	var clock := _clock()
	clock.arm_gate()

	assert_array(_admitted(clock, [1, 0, 1, 1, 0, 1])) \
			.override_failure_message(
				"a gated frame that emitted no tick bought no simulated time, "
				+ "so it must not advance the world",
			).is_equal([true, false, true, true, false, true])


# A tick worth two steps buys two frames, so a game running physics at twice its
# tick rate keeps both steps and the correspondence still holds exactly.
func test_a_tick_worth_two_steps_admits_two_frames() -> void:
	var clock := _clock(Engine.physics_ticks_per_second / 2)
	clock.arm_gate()
	assert_int(clock.physics_steps_per_tick).override_failure_message(
		"the arm needs a whole two steps per tick to mean anything",
	).is_equal(2)

	assert_array(_admitted(clock, [1, 0, 1, 0, 0, 1])) \
			.override_failure_message(
				"a tick worth two steps must pay for the frame that emitted it "
				+ "and the one after",
			).is_equal([true, true, true, true, false, true])


# Releasing the last gate restores an ungated clock rather than leaving the
# world held on whatever the final decision happened to be.
func test_releasing_the_last_gate_restores_every_frame() -> void:
	var clock := _clock()
	clock.arm_gate()
	clock.force_step(0)
	assert_bool(clock.is_simulating).is_false()

	clock.release_gate()

	assert_bool(clock.is_gated()).is_false()
	assert_bool(clock.is_simulating).override_failure_message(
		"a released gate must give the world its frames back",
	).is_true()


# Gates nest, because two predicted bodies in one session share one clock and
# either may leave first.
func test_gates_nest_and_only_the_last_release_ungates() -> void:
	var clock := _clock()
	clock.arm_gate()
	clock.arm_gate()

	clock.release_gate()
	assert_bool(clock.is_gated()).is_true()
	clock.force_step(0)
	assert_bool(clock.is_simulating).is_false()

	clock.release_gate()
	assert_bool(clock.is_gated()).is_false()


# The honest ceiling. A gated clock emits one tick per frame at most, so a peer
# whose physics cannot sustain the declared step rate cannot catch up by
# doubling a frame. It falls behind, and it says so.
func test_a_peer_that_cannot_keep_up_reports_it_instead_of_doubling() -> void:
	var clock := _clock()
	clock.arm_gate()
	# Three ticks' worth of time arriving in one frame is a peer three frames
	# behind the authority it tracks.
	clock.physics_step(clock.ticktime * 3.0)

	assert_int(clock.simulation_behind_count).override_failure_message(
		"a gated clock that could not emit the ticks it owed must report it",
	).is_greater(0)
	assert_bool(clock.is_simulating).override_failure_message(
		"falling behind must not also stop the world, or the peer can never "
		+ "work the backlog off",
	).is_true()


func test_an_ungated_clock_never_reports_falling_behind() -> void:
	var clock := _clock()
	clock.physics_step(clock.ticktime * 3.0)

	assert_int(clock.simulation_behind_count).is_equal(0)
