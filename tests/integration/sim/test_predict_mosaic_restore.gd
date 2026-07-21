## Laws for the reconstruction gate a correction consumes through.
##
## A masked stream opens with a whole gain-edge row and every later frame merges
## its changed fields over the last. So the moment the stream has seen that gain
## edge, the merged row is the sender's coherent row for its tick and a
## correction may consume it, however partial the mask that delivered it. Before
## the gain edge the merged row is a mosaic authoritative at no single tick, and a
## correction that rebased onto it would place the body in a pose that never
## existed and then compare against that pose next frame and compound. The gate
## is [code]_stream_reconstructed[/code]: armed by the first whole row, it lets a
## reconstructed partial row correct exactly as a whole row would; unarmed, it
## withholds. The strict bit-exact verifier keeps the tighter per-frame whole
## gate, since only a whole row can be fingerprinted against a same-tick
## prediction.
class_name TestPredictMosaicRestore
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const SNAP := PredictionComponent.CorrectionMode.SNAP


# Settles the predicted entity into steady state and returns the acknowledgement
# the warmup left, so a delivered frame reaches the compare rather than
# early-returning for want of a predicted state.
func _settled_ack(s: PredictionScenario, p: PredictedEntity) -> int:
	p.client_prediction.correction_mode = SNAP
	s.hold_input(p, RIGHT)
	s.run(20)
	s.reset_metrics(p)
	return p.server_state.reconcile_ack


# A state frame header for [param ack] carrying [param payload], marked whole or
# not. The payload is passed absolute so two entities can be handed the identical
# divergence.
func _frame(
		s: PredictionScenario,
		ack: int,
		payload: Dictionary,
		whole: bool,
) -> Dictionary:
	return {
		&"tick": s.client_clock.tick,
		&"ack": ack,
		&"payload": payload,
		&"whole": whole,
	}


# A payload far enough from [param p]'s prediction to trigger a correction.
func _divergent_payload(p: PredictedEntity) -> Dictionary:
	return { &"position": p.client_body.position + Vector2(500.0, 0.0) }


# The first law. Before the gain-edge row has landed the stream has not
# reconstructed, so a partial row is a mosaic and a correction declines it
# however far it appears to diverge. The body is left where the prediction put it.
func test_a_correction_never_consumes_a_pre_gain_edge_row() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var ack := _settled_ack(s, p)
	assert_int(ack).override_failure_message(
		"the warmup must produce an acknowledgement for this law to deliver one",
	).is_greater_equal(0)
	# Return the stream to its pre-gain-edge state: no whole row seen yet.
	p.client_prediction._engine()._stream_reconstructed = false
	var before := p.client_body.position

	p.client_prediction._engine()._on_state_frame(
		_frame(s, ack, _divergent_payload(p), false),
	)

	assert_int(p.corrections).override_failure_message(
		"before the gain edge a partial row describes no single tick and must "
		+ "not be restored from, however far it appears to diverge",
	).is_equal(0)
	assert_vector(p.client_body.position).override_failure_message(
		"a declined correction writes nothing, so the body stays where the "
		+ "prediction left it",
	).is_equal(before)
	await s.teardown()


# The second law, and the reason the first is not vacuous. Once the stream has
# reconstructed, a partial-mask row corrects, and it corrects to exactly where a
# whole row carrying the same values would. Two entities warmed identically
# receive the identical divergence, one partial and one whole, and land in the
# same place.
func test_a_reconstructed_row_corrects_like_a_whole_row() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p_partial := await s.add_predicted_entity()
	var p_whole := await s.add_predicted_entity()
	for p: PredictedEntity in [p_partial, p_whole]:
		p.client_prediction.correction_mode = SNAP
		s.hold_input(p, RIGHT)
	s.run(20)
	s.reset_metrics(p_partial)
	s.reset_metrics(p_whole)

	assert_bool(p_partial.client_prediction._engine()._stream_reconstructed) \
		.override_failure_message(
			"the whole warmup frames are the gain edge, so the stream must be "
			+ "armed before this law delivers a partial row",
		).is_true()
	var base := p_whole.client_body.position
	assert_vector(p_partial.client_body.position).override_failure_message(
		"the two entities are warmed identically, so their predicted bodies must "
		+ "coincide before the identical divergence is delivered",
	).is_equal(base)
	var payload := { &"position": base + Vector2(500.0, 0.0) }

	p_partial.client_prediction._engine()._on_state_frame(
		_frame(s, p_partial.server_state.reconcile_ack, payload, false),
	)
	p_whole.client_prediction._engine()._on_state_frame(
		_frame(s, p_whole.server_state.reconcile_ack, payload, true),
	)

	assert_int(p_partial.corrections).override_failure_message(
		"a reconstructed partial row past the gain edge must correct, or the "
		+ "gate is still withholding what it should now consume",
	).is_greater_equal(1)
	assert_int(p_whole.corrections).override_failure_message(
		"the whole-row control must correct too, or the divergence never "
		+ "triggered and this law measures nothing",
	).is_greater_equal(1)
	assert_vector(p_partial.client_body.position).override_failure_message(
		"a reconstructed row and a whole row carrying the same values must "
		+ "rebase the body to the same pose",
	).is_equal(p_whole.client_body.position)
	await s.teardown()


# The third law. A state row is recovery data, never verification: fingerprint
# verdicts ride the acknowledgement lane alone, so a state frame of either
# wholeness moves no verified count however divergent its payload.
func test_a_state_frame_reaches_no_fingerprint_verdict() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	var ack := _settled_ack(s, p)
	var verified_before := p.client_prediction.fp_verified_count

	p.client_prediction._engine()._on_state_frame(
		_frame(s, ack, _divergent_payload(p), false),
	)
	p.client_prediction._engine()._on_state_frame(
		_frame(s, ack, _divergent_payload(p), true),
	)
	assert_int(p.client_prediction.fp_verified_count).override_failure_message(
		"verification rides the acknowledgement lane, so a state frame must "
		+ "not reach a fingerprint verdict",
	).is_equal(verified_before)
	await s.teardown()
