## Laws that the recurrence depends on nothing outside its own antecedents.
##
## A transition is a function of the state before it and the command that drove
## it. Anything else it depends on is a fault waiting for the frame that exposes
## it, so these laws feed identical antecedents through different surroundings
## and hold the journal to producing identical fingerprints.
class_name TestPredictDeterminismLaws
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME
const _PredictTap := preload("res://addons/networked/replication/netw_predict_tap.gd")


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule = FRAME
	predicted.server_prediction.schedule = FRAME
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
	const Handle := NetwLagCompensationInterface.PredictionHandle

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


func test_a_frame_with_no_fresh_tick_repeats_rather_than_invents() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	_configure_frame(predicted)
	predicted.client_root.motion = Vector2.RIGHT

	for ticks in [1, 1, 1]:
		_emit_client_frame(scenario.client_clock, ticks)
	var driven := _journal_columns(predicted.client_prediction)

	# The FRAME tier drives once per frame whether or not a tick authored fresh
	# input, so a frame with nothing new still runs. What it must not do is
	# invent a command: it repeats the last one, and says so.
	var driven_hashes: PackedInt32Array = driven[&"c_hashes"]
	var last_fresh_hash := driven_hashes[driven_hashes.size() - 1]
	for index in 4:
		_emit_client_frame(scenario.client_clock, 0)

	var after := _journal_columns(predicted.client_prediction)
	assert_int(after[&"transitions"].size()).is_greater(
		driven[&"transitions"].size(),
	)

	var repeated := 0
	for i in range(driven[&"transitions"].size(), after[&"kinds"].size()):
		repeated += 1
		assert_int(after[&"kinds"][i]).override_failure_message(
			"a frame with no fresh input repeats the command it already has, "
			+ "so its row must say REPEAT rather than claim fresh input",
		).is_equal(NetwLagCompensationInterface.PredictionHandle.DriveKind.REPEAT)
		assert_int(after[&"c_hashes"][i]).override_failure_message(
			"a repeat runs the command it repeated, so it must hash to that "
			+ "same command and never to an invented one",
		).is_equal(last_fresh_hash)
	assert_int(repeated).is_greater(0)


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
