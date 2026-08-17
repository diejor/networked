## Law suite for what a [constant NetwPredict.Schedule.FRAME] pair's transition
## costs in physics frames.
##
## A frame-tier transition is a declared quantum of physics frames, and the
## declaration is the clock's own [member ClockCore.physics_factor]. A run that
## drives at any other cadence is measuring a body the game would never see.
class_name TestFrameSchedule
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const FRAMES := 40


func test_a_frame_pair_drives_at_the_quantum_its_clock_declares() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
		0.01,
		PredictionComponent.Schedule.FRAME,
	)
	s.hold_input(p, RIGHT)
	s.run_frames(FRAMES)

	var stats := p.client_prediction.stats
	assert_int(stats.quantum_declared).override_failure_message(
		"the scenario's clock declares its own physics factor, so a pair that "
		+ "reads a declaration of one is not on the frame tier at all",
	).is_equal(int(round(s.client_clock.physics_factor)))
	assert_int(stats.quantum_steps).override_failure_message(
		"the run spent %d frame(s) per transition against a declared %d"
		% [stats.quantum_steps, stats.quantum_declared],
	).is_equal(stats.quantum_declared)

	# A frame the clock held bought no simulated time, so it opens no
	# transition. Every frame between two ticks is one of those, which is what
	# makes the quantum a count of frames rather than of drives.
	assert_int(p.client_prediction.stats.authoring_clamped) \
			.override_failure_message(
				"a run whose every frame drove has collapsed the ratio the "
				+ "clock declares",
			).is_greater(0)


func test_a_pair_driven_off_its_declaration_charges_the_fault_on_both_books() \
		-> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
		0.01,
		PredictionComponent.Schedule.FRAME,
	)
	s.drive_off_quantum(1)
	s.hold_input(p, RIGHT)
	s.run_frames(FRAMES)

	var declared := int(round(s.client_clock.physics_factor))
	assert_int(p.client_prediction.stats.quantum_steps).is_less(declared)
	assert_int(p.client_prediction.stats.quantum_faults).is_greater(0)

	# The pool keeps the same account, and it can only keep it if it is told
	# the DECLARATION rather than the measurement: handed the measurement, its
	# declared and measured values are the same number and it can never charge
	# a fault at all.
	var pool := s.client_sim.native_drive_stats(p.client_entity)
	assert_int(pool[NetwPredictionEngine.STAT_QUANTUM_DECLARED]) \
			.override_failure_message(
				"the pool was told %d where the clock declares %d"
				% [pool[NetwPredictionEngine.STAT_QUANTUM_DECLARED], declared],
			).is_equal(declared)
	assert_int(pool[NetwPredictionEngine.STAT_QUANTUM_FAULTS]) \
			.override_failure_message(
				"the shell charged %d fault(s) and the pool charged %d"
				% [
					p.client_prediction.stats.quantum_faults,
					pool[NetwPredictionEngine.STAT_QUANTUM_FAULTS],
				],
			).is_greater(0)
