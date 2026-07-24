## Laws for what authority learns by checking the owner's own account of itself.
##
## The acknowledgement lane tells an owner whether authority agreed with it. This
## is the same question asked in the other direction, and only authority can act
## on the answer: it holds a timeline per peer, compares the fingerprint the owner
## claimed against the one authority reached, and charges the difference to an
## antecedent. Nothing is pushed back on the strength of it, so these laws pin
## what authority FOUND and never what it did about it.
class_name TestPredictServerVerification
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME
const Attribution := NetwPredictJournal.Attribution


## A body that can be made to step differently on one peer than on the other,
## without touching the command that drove it. That is what isolates the
## simulation antecedent: the commands stay equal and the world stays equal, so
## the only thing left to disagree about is the step.
class DriftingBody extends LagCompSimBody:
	var drift: Vector2 = Vector2.ZERO


	func _network_tick(delta: float, tick: int, is_fresh: bool) -> void:
		super._network_tick(delta, tick, is_fresh)
		position += drift


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.server_prediction.missing_policy = \
	NetwLagCompensationInterface.PredictionHandle.MissingInput.REPEAT_LAST
	for handle in [predicted.client_prediction, predicted.server_prediction]:
		handle.witness().contacts(
			func() -> Dictionary:
				return { colliders = [], sleeping = false },
		)


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()


func _deliver_command(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.client_prediction._engine().build_command_frame()
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


# Authority records the state its previous frame produced before driving the
# next one, which is the ordering that closes a journal row. The claim is judged
# against a closed row, so a rig that skipped this would judge nothing.
func _step_authority(
		scenario: PredictionScenario,
		predicted: PredictedEntity,
) -> void:
	scenario.server_sim.before_frame_step()
	predicted.server_prediction.simulate_frame(scenario.dt())


func _round(scenario: PredictionScenario, predicted: PredictedEntity) -> void:
	_emit_client_frame(scenario.client_clock, 1)
	_deliver_command(predicted)
	_step_authority(scenario, predicted)


func _run_first_boundary(
		client_sample: Dictionary,
		server_sample: Dictionary,
) -> int:
	var scenario := PredictionScenario.new()
	scenario.body_type = DriftingBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	if bool(client_sample.get(&"static_contact", false)):
		var client_contact := StaticBody2D.new()
		scenario.client.add_child(client_contact)
		client_sample[&"colliders"] = [client_contact]
		client_sample.erase(&"static_contact")
	if bool(server_sample.get(&"static_contact", false)):
		var server_contact := StaticBody2D.new()
		scenario.server.add_child(server_contact)
		server_sample[&"colliders"] = [server_contact]
		server_sample.erase(&"static_contact")
	predicted.client_prediction.witness().contacts(
		func() -> Dictionary:
			return client_sample.duplicate(true),
	)
	predicted.server_prediction.witness().contacts(
		func() -> Dictionary:
			return server_sample.duplicate(true),
	)
	var reported: Array[int] = []
	scenario.server_sim.peer_divergence.connect(
		func(_peer: int, _entry: int, attribution: Attribution) -> void:
			reported.append(attribution),
	)
	predicted.client_root.motion = Vector2.RIGHT
	for _i in 4:
		_round(scenario, predicted)
	assert_int(predicted.server_prediction.client_mismatch_count).is_equal(0)
	(predicted.client_root as DriftingBody).drift = Vector2(7.0, 0.0)
	for _i in 6:
		_round(scenario, predicted)
	assert_array(reported).override_failure_message(
		"the injected output drift must reach an attributed verdict",
	).is_not_empty()
	var first: int = reported.front()
	await scenario.teardown()
	return first


# The owner claims a fingerprint for every transition it has finished, so a run
# where the two peers agree still reaches verdicts. A rig that verified nothing
# would satisfy "no mismatch" without authority having checked anything, which is
# why the denominator is pinned before the numerator.
func test_authority_judges_a_clean_run_and_finds_nothing() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for _i in 8:
		_round(scenario, predicted)

	assert_int(predicted.server_prediction.client_fp_verified_count) \
			.override_failure_message(
				"authority must reach a verdict on the owner's claims, or the "
				+ "absence of a mismatch says nothing",
			).is_greater(0)
	assert_int(predicted.server_prediction.client_mismatch_count) \
			.override_failure_message(
				"two peers running the same command over the same state must not "
				+ "be reported as disagreeing",
			).is_equal(0)
	var compared_witnesses := 0
	var client_journal := predicted.client_prediction.journal()
	var server_journal := predicted.server_prediction.journal()
	for transition: int in server_journal.transitions():
		var client_row := client_journal.row_at(transition)
		var server_row := server_journal.row_at(transition)
		if client_row.is_empty() or server_row.is_empty():
			continue
		if (int(client_row[&"flags"]) & NetwPredictJournal.ROW_CLOSED) == 0:
			continue
		if (int(server_row[&"flags"]) & NetwPredictJournal.ROW_CLOSED) == 0:
			continue
		compared_witnesses += 1
		assert_int(client_row[&"topo_fp"]).is_equal(server_row[&"topo_fp"])
		assert_int(client_row[&"witness_fp"]) \
				.is_equal(server_row[&"witness_fp"])
		assert_bool(
			int(client_row[&"evidence_mask"]) \
			& NetwPredictJournal.EVIDENCE_WITNESS > 0,
		).is_true()
	assert_int(compared_witnesses).override_failure_message(
		"a clean run must compare at least one sealed witness boundary",
	).is_greater(0)
	await scenario.teardown()


# The owner's step is made to differ while its command and its world stay equal,
# which is exactly the shape authority cannot see from a state frame. It is
# caught, counted, and charged to the one antecedent left.
func test_authority_catches_an_owner_whose_step_disagrees() -> void:
	var scenario := PredictionScenario.new()
	scenario.body_type = DriftingBody
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var reported: Array[Dictionary] = []
	scenario.server_sim.peer_divergence.connect(
		func(peer: int, entry: int, attribution: Attribution) -> void:
			reported.append({
				&"peer": peer,
				&"entry": entry,
				&"attribution": attribution,
			}),
	)
	predicted.client_root.motion = Vector2.RIGHT

	for _i in 4:
		_round(scenario, predicted)
	var clean_verdicts := predicted.server_prediction.client_fp_verified_count
	# The same body, the same rig, no drift yet. Without this the law would pass
	# just as well if the two peers had never agreed in the first place, and the
	# drift would be proving nothing.
	assert_int(predicted.server_prediction.client_mismatch_count) \
			.override_failure_message(
				"this rig must agree before the drift, or the drift is not what "
				+ "the mismatch is measuring",
			).is_equal(0)
	(predicted.client_root as DriftingBody).drift = Vector2(7.0, 0.0)
	for _i in 6:
		_round(scenario, predicted)

	assert_int(predicted.server_prediction.client_mismatch_count) \
			.override_failure_message(
				"an owner whose step reaches a different state must be found, or "
				+ "authority is holding a timeline it never compares",
			).is_greater(0)
	assert_int(predicted.server_prediction.client_fp_verified_count) \
			.override_failure_message(
				"the run after the drift must still reach verdicts",
			).is_greater(clean_verdicts)
	assert_bool(reported.is_empty()).override_failure_message(
		"a mismatch authority counted must also be one it reported",
	).is_false()
	var first: Dictionary = reported.front()
	assert_int(first[&"peer"]).override_failure_message(
		"the report must name the peer whose claim was judged",
	).is_equal(predicted.client_entity.controller)
	# Equal observed boundaries leave the produced closure, so this verdict is a
	# claim about where the fault is and not a default.
	assert_int(first[&"attribution"]).override_failure_message(
		"a divergence under equal observed boundaries is charged to closure",
	).is_equal(Attribution.CLOSURE)
	await scenario.teardown()


func test_topology_is_the_first_unequal_boundary() -> void:
	var attribution := await _run_first_boundary(
		{colliders = [], sleeping = false, body_mode = 1},
		{colliders = [], sleeping = false, body_mode = 2},
	)
	assert_int(attribution).is_equal(Attribution.TOPOLOGY)


func test_contact_is_the_first_unequal_boundary() -> void:
	var attribution := await _run_first_boundary(
		{colliders = [], sleeping = false, static_contact = true},
		{colliders = [], sleeping = false},
	)
	assert_int(attribution).is_equal(Attribution.CONTACT)
