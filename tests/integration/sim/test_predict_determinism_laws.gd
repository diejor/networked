## Laws that the recurrence depends on nothing outside its own antecedents.
##
## A transition is a function of the state before it and the command that drove
## it. Anything else it depends on is a fault waiting for the frame that exposes
## it, so these laws feed identical antecedents through different surroundings
## and hold the journal to producing identical fingerprints.
class_name TestPredictDeterminismLaws
extends NetwTestSuite

const Handle := NetwLagCompensationInterface.PredictionHandle
const FRAME := Handle.Schedule.FRAME
const ContactClass := Handle.ContactClass
const WitnessClass := Handle.WitnessClass
const _PredictTap := preload("res://addons/networked/replication/netw_predict_tap.gd")


class TapHandle:
	extends RefCounted

	var tap_journal: NetwPredictJournal
	var tap_episode: Dictionary = { }


	func _init(value: NetwPredictJournal) -> void:
		tap_journal = value


	func journal() -> NetwPredictJournal:
		return tap_journal


	func stats() -> Dictionary:
		return { }


	func episode() -> Dictionary:
		return tap_episode.duplicate(true)


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	if ticks > 0:
		clock.force_step(ticks)
	clock.after_tick_loop.emit()


func _journal_columns(handle) -> Dictionary:
	var journal: NetwPredictJournal = handle.journal()
	return {
		&"transitions": journal.transitions(),
		&"c_hashes": journal.c_hashes(),
		&"post_fps": journal.post_fps(),
		&"kinds": journal.kinds(),
	}


func test_a_wired_entity_resolves_axes_that_agree_with_its_role() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)

	var client := predicted.client_prediction
	var server := predicted.server_prediction
	# The owning client authors its own command and its simulation is a guess.
	assert_int(client.input_source).is_equal(Handle.InputSource.LOCAL)
	assert_int(client.sim_mode).is_equal(Handle.SimMode.SPECULATIVE)

	# Authority reads a command another peer sent and its simulation is truth.
	assert_int(server.input_source).is_equal(Handle.InputSource.RECEIVED)
	assert_int(server.sim_mode).is_equal(Handle.SimMode.AUTHORITATIVE)

	assert_int(Handle.role_for_axes(client.input_source, client.sim_mode)) \
			.override_failure_message(
				"the role is the name of the axes, so a wired entity's axes "
				+ "must name the role it is actually running",
			).is_equal(Handle.Role.PREDICT)
	assert_int(Handle.role_for_axes(server.input_source, server.sim_mode)) \
			.is_equal(Handle.Role.CONSUME)


func test_two_entities_driven_alike_journal_alike() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var first := await scenario.add_predicted_entity()
	var second := await scenario.add_predicted_entity()
	_configure_frame(first)
	_configure_frame(second)

	# The same command every frame to both entities. Registration order, entity
	# id, and position in the runner's sweep are the only things that differ.
	first.client_root.motion = Vector2.RIGHT
	second.client_root.motion = Vector2.RIGHT
	for ticks in [1, 1, 1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)

	var a := _journal_columns(first.client_prediction)
	var b := _journal_columns(second.client_prediction)

	assert_array(a[&"transitions"]).is_not_empty()
	assert_array(b[&"c_hashes"]).override_failure_message(
		"the command is an antecedent of the transition, so two entities given "
		+ "the same command must hash it the same",
	).is_equal(a[&"c_hashes"])
	assert_array(b[&"post_fps"]).override_failure_message(
		"registration order is not an antecedent of the recurrence, so it "
		+ "cannot move a fingerprint",
	).is_equal(a[&"post_fps"])
	assert_array(b[&"kinds"]).is_equal(a[&"kinds"])
	assert_array(b[&"transitions"]).is_equal(a[&"transitions"])


func test_an_entity_journals_the_same_run_the_same_way_twice() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var before := _journal_columns(predicted.client_prediction)

	# Reading the journal is an observation, not a step. A read that moved a
	# fingerprint would make every capture a measurement of itself.
	var after := _journal_columns(predicted.client_prediction)

	assert_array(after[&"post_fps"]).override_failure_message(
		"the journal is the engine's own record, so reading it must not "
		+ "perturb the simulation it records",
	).is_equal(before[&"post_fps"])
	assert_array(after[&"c_hashes"]).is_equal(before[&"c_hashes"])


func test_a_frame_with_no_fresh_tick_is_clamped_before_the_journal() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var driven := _journal_columns(predicted.client_prediction)

	# Surplus callbacks cannot create simulation history authority has no matching
	# tick interval to consume. They resend the current command window only.
	for index in 4:
		_emit_client_frame(scenario.client_clock, 0)

	var after := _journal_columns(predicted.client_prediction)
	assert_array(after[&"transitions"]).is_equal(driven[&"transitions"])
	assert_array(after[&"c_hashes"]).is_equal(driven[&"c_hashes"])
	assert_int(predicted.client_prediction.authoring_clamped_count).is_equal(4)


func test_an_unjournaled_state_write_breaks_the_next_boundary() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var journal: NetwPredictJournal = predicted.client_prediction.journal()
	assert_int(journal.first_chain_break()).is_equal(-1)

	scenario.client_clock.before_tick_loop.emit()
	predicted.client_root.position += Vector2(7.0, 0.0)
	scenario.client_clock.force_step(1)
	scenario.client_clock.after_tick_loop.emit()

	var broken := journal.first_chain_break()
	assert_int(broken).override_failure_message(
		"a declared state write outside the transition kernel must name the "
		+ "first boundary whose pre-state no longer chains",
	).is_greater_equal(0)
	var row := journal.row_at(broken)
	assert_int(int(row.get(&"write_id", -1))).is_equal(0)
	assert_int(predicted.client_prediction.stats()[&"chain_breaks"]) \
			.is_equal(1)


func test_a_declared_witness_samples_after_the_drive() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var sampled_positions: Array[Vector2] = []
	predicted.client_prediction.witness().contacts(
		func() -> Dictionary:
			sampled_positions.append(predicted.client_root.position)
			return { colliders = [], sleeping = false },
	)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)

	assert_array(sampled_positions).is_not_empty()
	assert_float(sampled_positions.front().x).override_failure_message(
		"the witness is an output and must observe the state after the drive",
	).is_greater(0.0)
	var journal := predicted.client_prediction.journal()
	var row := journal.row_at(journal.last_closed())
	assert_bool(
		int(row[&"evidence_mask"]) & NetwPredictJournal.EVIDENCE_WITNESS > 0,
	).is_true()
	assert_array(row[&"witness_detail"][&"contact_classes"]).is_equal([
		NetwLagCompensationInterface.PredictionHandle.ContactClass.NONE,
	])
	assert_int(row[&"witness_detail"][&"solve_ordinal"]).is_equal(
		row[&"transition"],
	)


func test_a_breach_demotes_on_the_transition_that_realized_it() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var intruder: RigidBody2D = auto_free(RigidBody2D.new())
	scenario.client.add_child(intruder)
	var handle := predicted.client_prediction
	handle.witness().contacts(
		func() -> Dictionary:
			return { colliders = [intruder], sleeping = false },
	)
	handle.recovery().on_breach(Handle.BreachResponse.DEMOTE)

	var attempts := 0
	while attempts < 3:
		_emit_client_frame(scenario.client_clock, 1)
		attempts += 1
		if handle.sim_mode == Handle.SimMode.DISPLAY:
			break

	var episode := handle.episode()
	var transition := int(
		episode[&"disposition"][&"breach_transition"],
	)
	var row: Dictionary = episode[&"generator"][&"row"]
	assert_bool(row[&"witness_detail"][&"breach"]).is_true()
	assert_int(int(row[&"domain"])) \
			.is_equal(NetwPredictJournal.Domain.OUT_OF_DOMAIN)
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.DISPLAY)
	assert_int(int(episode[&"generator"][&"row"][&"transition"])) \
			.is_equal(transition)
	assert_bool(episode[&"disposition"][&"demoted"]).is_true()
	assert_int(int(episode[&"disposition"][&"breach_transition"])) \
			.is_equal(transition)
	assert_int(int(episode[&"writes"].back()[&"operator"])) \
			.is_equal(NetwPredictJournal.Operator.DEMOTE)


func test_static_contact_stays_in_boundary_under_the_solver_default() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var wall: StaticBody2D = auto_free(StaticBody2D.new())
	scenario.client.add_child(wall)
	var handle := predicted.client_prediction
	handle.archetype(Handle.Archetype.SOLVER_BODY)
	handle.witness().contacts(
		func() -> Dictionary:
			return { colliders = [wall], sleeping = false },
	)

	var attempts := 0
	while attempts < 3:
		_emit_client_frame(scenario.client_clock, 1)
		attempts += 1

	var row := handle.journal().row_at(handle.journal().last_closed())
	assert_bool(row[&"witness_detail"][&"breach"]).override_failure_message(
		"static world geometry is inside the boundary and must never breach",
	).is_false()
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.SPECULATIVE)
	assert_bool(handle.episode().is_empty()).is_true()


func test_the_solver_default_demotes_a_dynamic_breach() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var intruder: RigidBody2D = auto_free(RigidBody2D.new())
	scenario.client.add_child(intruder)
	var handle := predicted.client_prediction
	handle.archetype(Handle.Archetype.SOLVER_BODY)
	handle.witness().contacts(
		func() -> Dictionary:
			return { colliders = [intruder], sleeping = false },
	)

	var attempts := 0
	while attempts < 3:
		_emit_client_frame(scenario.client_clock, 1)
		attempts += 1
		if handle.sim_mode == Handle.SimMode.DISPLAY:
			break

	var row: Dictionary = handle.episode()[&"generator"][&"row"]
	assert_bool(row[&"witness_detail"][&"breach"]).is_true()
	assert_int(handle.sim_mode).is_equal(Handle.SimMode.DISPLAY)
	assert_bool(bool(handle.episode()[&"disposition"][&"demoted"])).is_true()


func test_the_witness_classifies_the_declared_island_boundary() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var support := StaticBody2D.new()
	var wall := StaticBody2D.new()
	var predicted_body := RigidBody2D.new()
	var outside_body := RigidBody2D.new()
	var proxy := AnimatableBody2D.new()
	var world := Node2D.new()
	NetwEntity.ensure(world).entity_id = &"world"
	world.add_child(support)
	scenario.client.add_child(world)
	scenario.client.add_child(wall)
	predicted.client_root.add_child(predicted_body)
	scenario.client.add_child(outside_body)
	scenario.client.add_child(proxy)
	var support_id := "path:%s" % support.get_path()
	predicted.client_prediction.sensors().sample(
		&"ground",
		func() -> Dictionary:
			return { collider = support_id },
	)
	predicted.client_prediction.witness().contacts(
		func() -> Dictionary:
			return {
				colliders = [support, wall, predicted_body, outside_body, proxy],
				sleeping = false,
			},
	)

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)

	var journal := predicted.client_prediction.journal()
	var detail: Dictionary = journal.row_at(
		journal.last_closed(),
	)[&"witness_detail"]
	assert_array(detail[&"contact_classes"]).is_equal([
		ContactClass.DECLARED_SUPPORT,
		ContactClass.OTHER_STATIC,
		ContactClass.PREDICTED_DYNAMIC,
		ContactClass.UNPREDICTED_DYNAMIC,
		ContactClass.KINEMATIC_PROXY,
	])
	assert_int(detail[&"contact_bucket"]).is_equal(4)
	assert_array(detail[&"collider_classes"]).is_equal([
		&"AnimatableBody2D", &"RigidBody2D", &"StaticBody2D",
	])
	assert_int(int(detail[&"witness_class_bits"])).is_equal(
		WitnessClass.SUPPORT | WitnessClass.STATIC \
		| WitnessClass.DYNAMIC_ENTITY,
	)


func test_witness_identity_ignores_the_local_dynamic_realization() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var remote_root := Node2D.new()
	NetwEntity.ensure(remote_root).entity_id = &"remote_car"
	var proxy := AnimatableBody2D.new()
	var dynamic := RigidBody2D.new()
	remote_root.add_child(proxy)
	remote_root.add_child(dynamic)
	scenario.client.add_child(remote_root)
	var use_proxy := { value = true }
	predicted.client_prediction.witness().contacts(
		func() -> Dictionary:
			return {
				colliders = [proxy if use_proxy.value else dynamic],
				sleeping = false,
			}
	)

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var journal := predicted.client_prediction.journal()
	var proxy_row := journal.row_at(journal.last_closed())
	use_proxy.value = false
	_emit_client_frame(scenario.client_clock, 1)
	var dynamic_row := journal.row_at(journal.last_closed())

	assert_int(int(proxy_row[&"witness_fp"])).override_failure_message(
		"the same entity contact must compare equally across peer realizations",
	).is_equal(int(dynamic_row[&"witness_fp"]))
	assert_array(proxy_row[&"witness_detail"][&"contact_classes"]).is_equal([
		ContactClass.KINEMATIC_PROXY,
	])
	assert_array(dynamic_row[&"witness_detail"][&"contact_classes"]).is_equal([
		ContactClass.UNPREDICTED_DYNAMIC,
	])
	assert_int(int(proxy_row[&"witness_class_bits"])).is_equal(
		WitnessClass.DYNAMIC_ENTITY,
	)
	assert_int(int(dynamic_row[&"witness_class_bits"])).is_equal(
		WitnessClass.DYNAMIC_ENTITY,
	)


func test_the_witness_records_sleep_and_wake_transitions() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	var sleep_state := {value = true}
	predicted.client_prediction.witness().contacts(
		func() -> Dictionary:
			return { colliders = [], sleeping = sleep_state.value },
	)
	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var journal := predicted.client_prediction.journal()
	var asleep: Dictionary = journal.row_at(
		journal.last_closed(),
	)[&"witness_detail"]
	assert_bool(asleep[&"sleeping"]).is_true()
	assert_bool(asleep[&"woke"]).is_false()

	sleep_state.value = false
	var first_awake_row := journal.last_closed() + 1
	for ticks in [1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var awake: Dictionary = journal.row_at(
		journal.last_closed(),
	)[&"witness_detail"]
	assert_bool(awake[&"sleeping"]).is_false()
	var observed_wake := false
	for row_index in range(first_awake_row, journal.last_closed() + 1):
		var detail: Dictionary = journal.row_at(row_index)[&"witness_detail"]
		observed_wake = observed_wake or bool(detail[&"woke"])
	assert_bool(observed_wake).is_true()


func test_the_raw_debug_gate_seals_exact_pre_state_bits() -> void:
	const RAW_ENV := "NETW_PREDICT_RAW_FP"
	var previous := OS.get_environment(RAW_ENV)
	OS.set_environment(RAW_ENV, "1")
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	OS.set_environment(RAW_ENV, previous)
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT
	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)

	var journal := predicted.client_prediction.journal()
	var row := journal.row_at(journal.last_closed())
	assert_bool(
		int(row[&"evidence_mask"]) & NetwPredictJournal.EVIDENCE_RAW > 0,
	).is_true()
	assert_int(row[&"raw_fp"]).is_not_equal(0)


func test_the_tap_drain_reads_the_journal_without_perturbing_it() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var before := _journal_columns(predicted.client_prediction)
	assert_array(before[&"transitions"]).override_failure_message(
		"the run must journal transitions for this law to have something to drain",
	).is_not_empty()

	# The tap is the journal-capture instrument, and its whole contract is that
	# draining is an observation and not a step. A drain that moved a fingerprint
	# would make every manual capture a measurement of itself.
	var dir := "user://predict_tap_test"
	var tap := _PredictTap.new(dir)
	tap.drain(&"probe", predicted.client_prediction)
	tap.close()

	var after := _journal_columns(predicted.client_prediction)
	assert_array(after[&"post_fps"]).override_failure_message(
		"the tap consumes only the public read surface, so draining it must not "
		+ "move a fingerprint the run recorded",
	).is_equal(before[&"post_fps"])
	assert_array(after[&"c_hashes"]).is_equal(before[&"c_hashes"])
	assert_array(after[&"transitions"]).is_equal(before[&"transitions"])

	# The drain actually wrote, so the read-only law is not measuring a no-op.
	var path := dir.path_join("probe.jsonl")
	assert_bool(FileAccess.file_exists(path)).override_failure_message(
		"the drain must leave a capture file, or it read nothing to be pure about",
	).is_true()
	assert_str(FileAccess.get_file_as_string(path)).is_not_empty()
	DirAccess.remove_absolute(path)


func test_the_tap_exports_each_sealed_row_then_its_final_disposition() -> void:
	var dir := "user://predict_tap_two_phase_test"
	var path := dir.path_join("probe.jsonl")
	var journal := NetwPredictJournal.new(2)
	var handle := TapHandle.new(journal)
	var tap := _PredictTap.new(dir)

	journal.open(0, 0, 1, 100)
	journal.close(0, 200)
	tap.drain(&"probe", handle)
	journal.mark_aligned_error(0, 1.5)
	journal.mark_ack(0, false)
	tap.drain(&"probe", handle)

	journal.open(1, 1, 1, 101)
	journal.close(1, 201)
	tap.drain(&"probe", handle)
	journal.open(2, 2, 1, 102)
	journal.close(2, 202)
	journal.open(3, 3, 1, 103)
	journal.close(3, 203)
	tap.drain(&"probe", handle)
	journal.mark_ack(2, true)
	journal.mark_substituted(3)
	tap.drain(&"probe", handle)
	tap.drain(&"probe", handle)
	tap.close()

	var sealed: Array[int] = []
	var settled: Array[int] = []
	var aligned_errors: Array[float] = []
	var lost: Array[int] = []
	for line in FileAccess.get_file_as_string(path).split("\n", false):
		var record: Dictionary = JSON.parse_string(line)
		for row: Dictionary in record.get("rows", []):
			sealed.append(int(row.get("transition", -1)))
		for disposition: Dictionary in record.get("settles", []):
			var transition := int(disposition.get("transition", -1))
			if bool(disposition.get("lost", false)):
				lost.append(transition)
			else:
				settled.append(transition)
				aligned_errors.append(
					float(disposition.get("aligned_error", -1.0)),
				)

	assert_array(sealed).override_failure_message(
		"a sealed row is exported once, before its mutable verdict overlay",
	).is_equal([0, 1, 2, 3])
	assert_array(settled).override_failure_message(
		"retained rows receive exactly one final disposition",
	).is_equal([0, 2, 3])
	assert_array(aligned_errors).override_failure_message(
		"each settlement carries continuous aligned error outside episodes",
	).is_equal([1.5, 0.0, 0.0])
	assert_array(lost).override_failure_message(
		"eviction before settlement must be visible rather than silent",
	).is_equal([1])
	DirAccess.remove_absolute(path)


func test_the_tap_exports_an_episode_change_without_a_new_row() -> void:
	var dir := "user://predict_tap_episode_test"
	var path := dir.path_join("probe.jsonl")
	var handle := TapHandle.new(NetwPredictJournal.new())
	var tap := _PredictTap.new(dir)
	handle.tap_episode = {
		&"id": 7,
		&"reopen_chain": [3, 7],
	}

	tap.drain(&"probe", handle)
	tap.drain(&"probe", handle)
	tap.close()

	var lines := FileAccess.get_file_as_string(path).split("\n", false)
	assert_int(lines.size()).override_failure_message(
		"one changed episode must emit once even when no journal row changed",
	).is_equal(1)
	var record: Dictionary = JSON.parse_string(lines[0])
	assert_int(int(record["episode"]["id"])).is_equal(7)
	var reopen_chain: Array[int] = []
	for value: Variant in record["episode"]["reopen_chain"]:
		reopen_chain.append(int(value))
	assert_array(reopen_chain).is_equal([3, 7])
	DirAccess.remove_absolute(path)
