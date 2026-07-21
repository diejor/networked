## Laws for a rollback that restores a declared island rather than one entity.
##
## The conformance case behind these is the one every rollback library eventually
## meets: one entity writes to another inside its own step, and a correction
## rewinds past the write. Rewinding only the corrected entity leaves the write
## standing as a stale copy of a result its cause no longer produces. Rewinding
## both and re-running only the corrected one erases the write entirely. The
## island is restored and re-run together so the write is REGENERATED, which is
## the only outcome that agrees with a run that never needed correcting.
class_name TestIslandRollback
extends NetwTestSuite

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const FRAME := PredictionHandle.Schedule.FRAME
const RecoveryPolicy := PredictionHandle.RecoveryPolicy
const PUSH := Vector2(0.0, 100.0)


## A body that writes to another entity inside its own step, the cross-entity
## mutation the island exists to describe.
##
## The write happens at ONE transition and accumulates, which is what makes it a
## real test of the rollback. A write repeated every step would be restored by
## re-running the pusher alone, so it could not tell a scope rollback from a lone
## rebase. A one-time accumulating write can only come out right if the entity it
## landed on was rolled back too.
class PusherBody extends LagCompSimBody:
	var target: Node2D = null
	var push_at: int = -1
	var pushes: int = 0


	func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
		super._network_tick(delta, tick, is_fresh)
		if target and tick == push_at:
			target.position += PUSH
			pushes += 1


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule = FRAME
	predicted.server_prediction.schedule = FRAME
	predicted.server_prediction.replay_buffer_depth = 1


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()


# Builds the pusher/pushed pair. The pusher is added first so it sorts first by
# entity id, which is the order its write depends on.
func _island(scenario: PredictionScenario) -> Array:
	scenario.body_type = PusherBody
	var pusher := await scenario.add_predicted_entity()
	var pushed := await scenario.add_predicted_entity()
	_configure_frame(pusher)
	_configure_frame(pushed)
	(pusher.client_root as PusherBody).target = pushed.client_root
	pusher.client_prediction.configure_island({
		participants = [pushed.client_entity],
	})
	pusher.client_prediction.configure_recovery({
		policy = RecoveryPolicy.ROLLBACK_SCOPE,
	})
	return [pusher, pushed]


# Drives the pair until the one-time write has landed, then rewinds the pusher to
# an entry before it. Returns the entry the rollback was taken to.
func _run_and_roll_back(
		scenario: PredictionScenario,
		pusher: PredictedEntity,
		pushed: PredictedEntity,
) -> int:
	pusher.client_root.motion = Vector2.RIGHT
	for _i in 2:
		_emit_client_frame(scenario.client_clock, 1)
	(pusher.client_root as PusherBody).push_at = scenario.client_clock.tick + 1
	for _i in 4:
		_emit_client_frame(scenario.client_clock, 1)

	# The rig is only a test of the rollback if the write actually landed, once.
	assert_int((pusher.client_root as PusherBody).pushes).override_failure_message(
		"the pusher must have written to the pushed entity exactly once before "
		+ "the rollback, or there is no cross-entity write under test",
	).is_equal(1)
	assert_vector(pushed.client_root.position).override_failure_message(
		"the pushed entity must be holding exactly one push",
	).is_equal(PUSH)

	var rollback_to := 1
	var truth := pusher.client_prediction.transition_state_at(rollback_to)
	assert_dict(truth).override_failure_message(
		"the rollback point must name a recorded entry",
	).is_not_empty()
	# Authority disagrees with the pusher at an entry BEFORE the write, so the
	# resimulation has to carry the correction forward through it.
	var corrected: Dictionary = truth.duplicate()
	corrected[&"position"] = (truth[&"position"] as Vector2) + Vector2(0.0, 25.0)
	# The injected row is a whole authoritative row, so arm the reconstruction
	# gate exactly as the state frame that would carry it does.
	pusher.client_prediction._engine()._stream_reconstructed = true
	pusher.client_prediction._engine()._on_state(
		scenario.client_clock.tick,
		rollback_to,
		corrected,
	)
	assert_int(pusher.client_prediction.corrections).override_failure_message(
		"the rig must actually correct the pusher, or nothing was rolled back",
	).is_greater(0)
	return rollback_to


# The conformance law. The island is restored together, so the pushed entity is
# rewound past the write and the replay lands it again: exactly one push, not the
# two that come from re-applying a write to a body still holding the first one.
func test_a_cross_entity_write_is_regenerated_exactly_once() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var parts: Array = await _island(scenario)
	var pusher: PredictedEntity = parts[0]
	var pushed: PredictedEntity = parts[1]

	_run_and_roll_back(scenario, pusher, pushed)

	assert_int((pusher.client_root as PusherBody).pushes).override_failure_message(
		"the replay must re-run the entry that wrote, or the write was not "
		+ "regenerated at all",
	).is_equal(2)
	assert_vector(pushed.client_root.position).override_failure_message(
		"the pushed entity must hold ONE push after the rollback. Two means it "
		+ "was never rewound and the replayed write compounded onto the first",
	).is_equal(PUSH)
	await scenario.teardown()


# The mirror, and the reason the scope is worth having. The same rig under a lone
# rebase re-runs the pusher without rewinding what it wrote to, so the replayed
# write lands on a body still holding the first one and the push counts twice.
# Without this law the one above would pass on a build that rewound nothing.
func test_a_lone_rebase_compounds_the_write_it_replays() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var parts: Array = await _island(scenario)
	var pusher: PredictedEntity = parts[0]
	var pushed: PredictedEntity = parts[1]
	pusher.client_prediction.configure_recovery({
		policy = RecoveryPolicy.REBASE_REPLAY,
	})

	_run_and_roll_back(scenario, pusher, pushed)

	assert_vector(pushed.client_root.position).override_failure_message(
		"a lone rebase rewinds only the entity it corrected, so the write it "
		+ "replays must land on top of the one already there",
	).is_equal(PUSH * 2.0)
	await scenario.teardown()
