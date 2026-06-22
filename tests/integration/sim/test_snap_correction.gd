## SNAP vs REPLAY correction modes (lag-comp Tier 2 dynamic-body support).
##
## A kinematic body's step is a callable, so it replays unacked inputs over a
## restored state. A dynamic body's solver cannot be stepped per input, so it
## snaps to authoritative state and stops, letting the solver carry restored
## velocity forward (real physics) and the display chase absorb the snap.
##
## Resolution by body type is a pure check on
## [method PredictionComponent.resolve_correction_mode_for]. SNAP's behavior (it
## adopts authoritative state without walking the replay window) is driven on the
## closed-form rig with the resolved mode forced to SNAP, since a real RigidBody
## does not fit [PredictionScenario] (its [LockstepStepper] never steps physics).
class_name TestSnapCorrection
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const AUTO := PredictionComponent.CorrectionMode.AUTO
const REPLAY := PredictionComponent.CorrectionMode.REPLAY
const SNAP := PredictionComponent.CorrectionMode.SNAP


func test_auto_resolves_snap_for_rigidbody() -> void:
	var body: RigidBody2D = auto_free(RigidBody2D.new())
	assert_int(PredictionComponent.resolve_correction_mode_for(body, AUTO)).is_equal(SNAP)


func test_auto_resolves_replay_for_kinematic() -> void:
	var body: CharacterBody2D = auto_free(CharacterBody2D.new())
	assert_int(PredictionComponent.resolve_correction_mode_for(body, AUTO)).is_equal(REPLAY)


func test_auto_resolves_replay_for_plain_node() -> void:
	var body: Node2D = auto_free(Node2D.new())
	assert_int(PredictionComponent.resolve_correction_mode_for(body, AUTO)).is_equal(REPLAY)


func test_explicit_mode_overrides_body_type() -> void:
	var body: RigidBody2D = auto_free(RigidBody2D.new())
	assert_int(PredictionComponent.resolve_correction_mode_for(body, REPLAY)).is_equal(REPLAY)


func test_kinematic_scenario_body_resolves_replay() -> void:
	# The closed-form scenario body is a Node2D, so it resolves REPLAY end to end.
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()

	assert_int(p.client_prediction.resolved_correction_mode()).is_equal(REPLAY)


func test_snap_adopts_authority_without_replay() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	# Force SNAP on the working closed-form rig: the no-replay branch is body-type
	# independent, so this exercises it without a real physics body.
	p.client_prediction._correction = SNAP
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(30)
	# The y nudge is something the client never predicted (input is RIGHT only),
	# so it only lands on the body if SNAP adopted the authoritative state.
	s.perturb_server(p, Vector2(60.0, -40.0))
	s.run(70)

	assert_int(p.corrections).is_greater_equal(1)
	# SNAP restores authoritative state and stops, so the replay window is never
	# walked. This is the one behavioral split from the kinematic REPLAY path.
	assert_int(p.max_replay_depth).is_equal(0)
	# The client adopted the server-only y nudge through the restore.
	assert_float(p.client_body.position.y).is_less(0.0)
