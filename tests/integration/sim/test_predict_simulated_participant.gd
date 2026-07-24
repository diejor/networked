## Runtime laws for the predicted-command speculative island cell.
class_name TestPredictSimulatedParticipant
extends NetwTestSuite

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const InputSource := PredictionHandle.InputSource
const SimMode := PredictionHandle.SimMode
const Role := PredictionHandle.Role


func test_explicit_promotion_steps_a_remote_with_predicted_commands() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var subject := await scenario.add_predicted_entity()
	var participant := await scenario.add_predicted_entity()
	_make_remote(participant)
	var predictor := func(_entity: NetwEntity, _tick: int) -> Dictionary:
		return { &"motion": Vector2.RIGHT }
	subject.client_prediction.island().simulate(
		participant.client_entity,
		predictor,
	)
	scenario.latency_both(4)

	scenario.run(1)

	assert_int(participant.client_prediction.input_source) \
			.is_equal(InputSource.PREDICTED)
	assert_int(participant.client_prediction.sim_mode) \
			.is_equal(SimMode.SPECULATIVE)
	assert_int(PredictionHandle.role_for_axes(
		participant.client_prediction.input_source,
		participant.client_prediction.sim_mode,
	)).is_equal(Role.SIMULATE)
	assert_float(participant.client_root.position.x).is_greater(0.0)
	assert_int(participant.client_prediction.substituted_count).is_equal(1)


func test_coast_zeros_stale_remote_input() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var subject := await scenario.add_predicted_entity()
	var participant := await scenario.add_predicted_entity()
	participant.client_root.motion = Vector2.RIGHT
	_make_remote(participant)
	subject.client_prediction.island().simulate(participant.client_entity)

	scenario.run(1)

	assert_vector(participant.client_root.position).is_equal(Vector2.ZERO)
	assert_vector(participant.client_root.motion).is_equal(Vector2.ZERO)


func test_each_received_state_rebases_the_simulated_body() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var subject := await scenario.add_predicted_entity()
	var participant := await scenario.add_predicted_entity()
	_make_remote(participant)
	subject.client_prediction.island().simulate(participant.client_entity)
	scenario.run(1)
	participant.client_prediction.snap_restore = PredictionHandle.RestoreMode.EXACT
	participant.client_root.position = Vector2(20.0, 0.0)
	var corrections_before := participant.client_prediction.corrections

	participant.client_prediction._engine()._on_simulated_state_frame({
		&"whole": true,
		&"tick": scenario.client_clock.tick,
		&"payload": { &"position": Vector2(3.0, 0.0) },
	})

	assert_vector(participant.client_root.position) \
			.is_equal(Vector2(3.0, 0.0))
	assert_int(participant.client_prediction.corrections) \
			.is_equal(corrections_before + 1)
	assert_dict(participant.client_prediction.episode()).is_empty()


func test_interest_producer_promotes_only_locally_replicated_members() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var subject := await scenario.add_predicted_entity()
	var participant := await scenario.add_predicted_entity()
	var outside := await scenario.add_predicted_entity()
	_make_remote(participant)
	_make_remote(outside)
	var race := scenario.client.api.interest.layer(&"race")
	race._client_track_entity(subject.client_entity)
	race._client_track_entity(participant.client_entity)
	subject.client_prediction.island() \
			.approximate() \
			.from_interest() \
			.simulate_nearest(1)

	scenario.run(1)

	assert_int(participant.client_prediction.sim_mode) \
			.is_equal(SimMode.SPECULATIVE)
	assert_int(outside.client_prediction.sim_mode).is_equal(SimMode.DISPLAY)
	assert_array(subject.client_prediction.stats()[&"island_members"]) \
			.contains_exactly([String(participant.client_entity.entity_id)])
	assert_array(subject.client_prediction.stats()[&"simulated_members"]) \
			.contains_exactly([String(participant.client_entity.entity_id)])


func test_nearest_promotion_is_hysteretic_and_defers_contact_handoff() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var subject := await scenario.add_predicted_entity()
	var first := await scenario.add_predicted_entity()
	var second := await scenario.add_predicted_entity()
	_make_remote(first)
	_make_remote(second)
	scenario.latency_both(100)
	subject.client_root.position = Vector2.ZERO
	first.client_root.position = Vector2(10.0, 0.0)
	second.client_root.position = Vector2(10.5, 0.0)
	subject.client_prediction.island() \
			.add(first.client_entity) \
			.add(second.client_entity) \
			.simulate_nearest(1)
	scenario.run(1)
	assert_int(first.client_prediction.sim_mode).is_equal(SimMode.SPECULATIVE)

	second.client_root.position = Vector2(9.5, 0.0)
	scenario.run(1)
	assert_int(first.client_prediction.sim_mode).override_failure_message(
		"the current winner stays promoted inside the ten-percent exit margin",
	).is_equal(SimMode.SPECULATIVE)

	second.client_root.position = Vector2(5.0, 0.0)
	var engine := subject.client_prediction._engine()
	engine._realized_contact_entities[first.client_entity] = true
	scenario.run(1)
	assert_int(first.client_prediction.sim_mode).override_failure_message(
		"an active contact defers the outgoing body-mode flip",
	).is_equal(SimMode.SPECULATIVE)
	assert_int(second.client_prediction.sim_mode).is_equal(SimMode.DISPLAY)

	engine._realized_contact_entities.clear()
	scenario.run(1)
	assert_int(first.client_prediction.sim_mode).is_equal(SimMode.DISPLAY)
	assert_int(second.client_prediction.sim_mode).is_equal(SimMode.SPECULATIVE)


# Moves the matched entity onto a controller absent from this client.
func _make_remote(participant: PredictedEntity) -> void:
	participant.server_entity.controller = 991
	participant.client_entity.controller = 991
