## Law suite for what a watched session is told about one recovery episode: the
## disagreement, the episode opened to supervise it, the write that answered it,
## and the close that retires it.
##
## The owner is the peer that has an episode at all, so the watch goes on the
## client. The rows are read through a sink rather than through the ring,
## because a scenario long enough to close an episode has already pushed the
## opening out of a bounded per-route ring.
class_name TestEpisodeReports
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const OFFSET := Vector2(60, -40)

var _reported: Array[NetwEvent] = []
# The authority's own rows, kept apart because a pass is reported from both
# sides and the two sides report different stages of it.
var _authority: Array[NetwEvent] = []


func before_test() -> void:
	_reported = []
	_authority = []


func test_an_episode_reports_its_disagreement_its_write_and_its_close() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	_watch(s)
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(30)

	s.perturb_server(p, OFFSET)
	s.run(70)

	# The disagreement is reported before the episode, because a divergence is
	# a comparison's own verdict and an episode is what the pool does about it.
	assert_int(_reported.size()).is_greater_equal(4)
	assert_int(_reported[0].event).is_equal(NetwMultiplayerCore.DIVERGENCE)
	assert_int(_reported[1].event).is_equal(NetwMultiplayerCore.EPISODE_OPEN)
	assert_int(_reported[2].event).is_equal(NetwMultiplayerCore.RECOVERY)
	assert_int(_reported[-1].event).is_equal(NetwMultiplayerCore.EPISODE_CLOSE)

	var transition := int(_reported[0].detail.get(&"transition", -1))
	assert_int(transition).is_greater_equal(0)
	assert_float(float(_reported[0].detail.get(&"divergence", 0.0))) \
			.is_greater(0.0)
	assert_int(int(_reported[1].detail.get(&"transition", -1))) \
			.is_equal(transition)
	assert_bool(_reported[1].model.has(&"generator")).is_true()

	# The write answers that disagreement and moves the body by what authority
	# was displaced by, so the row states the correction rather than only that
	# one happened.
	var written := _reported[2]
	assert_int(int(written.detail.get(&"transition", -1))).is_equal(transition)
	assert_bool(bool(written.detail.get(&"teleport", true))).is_false()
	var moved: Dictionary = written.detail.get(&"moved", { })
	assert_bool(moved.has(&"position")).is_true()
	assert_float((moved[&"position"] as Vector2).distance_to(OFFSET)) \
			.is_less(0.01)

	# Acknowledgements already in flight when the correction was written still
	# disagree, so a divergence after the first names a later transition and
	# earns no second write.
	for row in _reported.slice(3, _reported.size() - 1):
		assert_int(row.event).is_equal(NetwMultiplayerCore.DIVERGENCE)
		assert_int(int(row.detail.get(&"transition", -1))).is_greater(transition)

	var closed := _reported[-1]
	assert_int(int(closed.detail.get(&"transition", -1))).is_equal(transition)
	assert_int(int(closed.detail.get(&"state", -1))).is_equal(
		NetwPredict.EpisodeState.CLOSED
	)
	# An episode retires on a transition later than the one that opened it,
	# because what retires it is the agreement run it had to assemble first.
	assert_int(int(closed.detail.get(&"closed_transition", -1))) \
			.is_greater(transition)


# The client is the predicting peer, so it is the one that has an episode.
func _watch(s: PredictionScenario) -> void:
	s.client.api._native_core.event_watch(
		[
			NetwMultiplayerCore.EPISODE_OPEN,
			NetwMultiplayerCore.EPISODE_CLOSE,
			NetwMultiplayerCore.EPISODE_FALLBACK,
			NetwMultiplayerCore.DIVERGENCE,
			NetwMultiplayerCore.RECOVERY,
		],
		{ },
		{ },
		_on_event,
	)


func _on_event(event: NetwEvent) -> void:
	_reported.append(event)


func test_a_pass_reports_the_stage_it_drove_and_what_it_consumed() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	_watch_stages(s)
	s.hold_input(p, RIGHT)
	s.run(20)

	# Every driven pass files the transition it opened and the input it drove,
	# so a watcher counts drives without reading the tape.
	var drives := _rows_of(_reported, NetwMultiplayerCore.PREDICT_DRIVE)
	assert_int(drives.size()).is_greater(0)
	assert_bool(bool(drives[0].detail.get(&"fresh"))).is_true()
	assert_int(int(drives[0].detail.get(&"transition", -1))).is_greater_equal(0)

	# Consuming is the other side of the same pass, and only authority does it,
	# so the owner never files one and the server files one per tick.
	assert_int(
		_rows_of(_reported, NetwMultiplayerCore.PREDICT_CONSUME).size()
	).is_equal(0)
	var consumes := _rows_of(_authority, NetwMultiplayerCore.PREDICT_CONSUME)
	assert_int(consumes.size()).is_greater(0)
	assert_int(int(consumes[0].detail.get(&"buffer", -1))).is_greater_equal(0)
	assert_bool(consumes.any(func(row: NetwEvent) -> bool:
		return int(row.detail.get(&"action", -1)) \
				== NetwPredict.ConsumeAction.REPLAY
	)).is_true()


func test_a_judged_transition_reports_the_stages_that_judged_and_answered_it() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	_watch_stages(s)
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(30)

	s.perturb_server(p, OFFSET)
	s.run(70)

	# A comparison reports what it judged whether or not it corrected, which is
	# what separates the stage from the divergence it sometimes finds.
	var judged := _rows_of(_reported, NetwMultiplayerCore.PREDICT_EVALUATE)
	assert_int(judged.size()).is_greater(1)
	assert_bool(judged.any(func(row: NetwEvent) -> bool:
		return not bool(row.detail.get(&"corrected", true))
	)).is_true()

	# The recovery stage reports every plan it staged, including the ones that
	# declined to write, so a correction that never happened is still a row.
	var staged := _rows_of(_reported, NetwMultiplayerCore.PREDICT_RECOVER)
	assert_int(staged.size()).is_greater(0)
	assert_bool(staged.any(func(row: NetwEvent) -> bool:
		return not bool(row.detail.get(&"skip", true))
	)).is_true()


func _watch_stages(s: PredictionScenario) -> void:
	var stages := [
		NetwMultiplayerCore.PREDICT_DRIVE,
		NetwMultiplayerCore.PREDICT_CONSUME,
		NetwMultiplayerCore.PREDICT_EVALUATE,
		NetwMultiplayerCore.PREDICT_RECOVER,
	]
	s.client.api._native_core.event_watch(stages, { }, { }, _on_event)
	s.server.api._native_core.event_watch(stages, { }, { }, _on_authority_event)


func _rows_of(rows: Array[NetwEvent], event: int) -> Array[NetwEvent]:
	var kept: Array[NetwEvent] = []
	for row in rows:
		if row.event == event:
			kept.append(row)
	return kept


func _on_authority_event(event: NetwEvent) -> void:
	_authority.append(event)
