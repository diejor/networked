## Laws for what it costs to read an open episode every frame.
##
## An episode has no bound on how long it may stay open, so a reader that runs
## every frame must cost the same at every age. An instrument whose cost scales
## with the condition it reports cannot separate the system from itself: it slows
## the peer exactly when the peer is already in trouble, and a campaign measuring
## that peer would be measuring its own observer.
##
## Three readers run per frame on a live session, and all three used to detach
## the whole record: the boundary overlay, the prediction tap, and a game's own
## recorder. This pins the cost each of them now pays and prints the curve the
## detached report still follows, so the gap between them stays visible.
class_name TestPredictObserverCost
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const Handle := NetwLagCompensationInterface.PredictionHandle
const Attribution := NetwPredictJournal.Attribution

# Episode ages the curve is read at. The rendered session that opened this
# question ran an episode open for 11.2 s at roughly 50 settled comparisons a
# second, so the middle of this range is the measured case rather than a
# synthetic worst one.
const _AGES: Array[int] = [32, 256, 1024, 4096]
# Batches per measurement, medianed, so one scheduling hiccup cannot decide a
# law. Each batch times enough calls to sit well above the clock's resolution.
const _BATCHES := 5
const _CALLS := 200


func _engine() -> NetwLagCompensationInterface._PredictionEngine:
	var engine := Engine_.new()
	engine._handle = Handle.new()
	engine._journal.open(0, 0, Handle.DriveKind.FRESH, 1)
	engine._journal.close(0, 1)
	engine._forensic_sidecars[0] = {
		&"pre_bytes": PackedByteArray([1]),
		&"post_bytes": PackedByteArray([2]),
	}
	engine._open_episode(0, Attribution.PRE_STATE)
	return engine


# Grows the contraction series past its retained window by appending straight to
# it. The trim is what keeps a live episode bounded; this measures the readers,
# so it deliberately builds the sizes the readers used to be handed.
func _grow(engine: NetwLagCompensationInterface._PredictionEngine, n: int) -> void:
	var comparisons: Array = engine._episode[&"comparisons"]
	while comparisons.size() < n:
		comparisons.append({
			&"transition": comparisons.size(),
			&"meter": 1,
			&"agrees": false,
			&"write_id": 0,
		})


# Median microseconds for one call of [param body], measured in batches.
func _micros_per_call(body: Callable) -> float:
	var samples: Array[float] = []
	for _batch in _BATCHES:
		var start := Time.get_ticks_usec()
		for _call in _CALLS:
			body.call()
		samples.append(float(Time.get_ticks_usec() - start) / float(_CALLS))
	samples.sort()
	return samples[samples.size() / 2]


# The fence. A per-frame reader that grew with episode age is the defect this
# law exists to keep closed, so the threshold is loose enough that only a return
# to detaching the record can trip it: the detached report at this ratio of
# sizes costs two orders of magnitude more, not eight times more.
func test_the_digest_cost_does_not_grow_with_episode_age() -> void:
	var engine := _engine()
	var handle := engine._handle
	_grow(engine, _AGES[0])
	var young := _micros_per_call(handle.episode_digest)
	_grow(engine, _AGES[-1])
	var old := _micros_per_call(handle.episode_digest)

	assert_float(old).override_failure_message(
		(
				"reading an episode %d comparisons deep cost %.3f us against "
				+ "%.3f us at %d deep, so the per-frame reader grows with the "
				+ "condition it reports"
		) % [_AGES[-1], old, young, _AGES[0]],
	).is_less(maxf(young, 0.5) * 8.0)


# The same fence on the write side. Evidence used to be copied once per entry
# added to it, which is quadratic in the age of an open episode and lands on
# every game rather than only on one with an instrument armed.
func test_recording_evidence_does_not_grow_with_episode_age() -> void:
	var engine := _engine()
	var transition := 1
	var record := func() -> void:
		engine._record_episode_comparison(transition, 1, false)
		transition += 1
	_grow(engine, _AGES[0])
	var young := _micros_per_call(record)
	_grow(engine, _AGES[-1])
	var old := _micros_per_call(record)

	assert_float(old).override_failure_message(
		(
				"recording one comparison into an episode %d deep cost %.3f us "
				+ "against %.3f us at %d deep, so evidence is being copied "
				+ "rather than appended"
		) % [_AGES[-1], old, young, _AGES[0]],
	).is_less(maxf(young, 0.5) * 8.0)


# The curve itself, printed rather than asserted. The detached report is the
# shape the three per-frame readers used to follow, and one evidence mutation
# used to pay the copy column on its own, once per entry, for the life of the
# episode.
func test_reports_the_observer_cost_curve() -> void:
	print(
		"[observer] comparisons   digest us   report us   per-mutation copy us"
		+ "   report/digest",
	)
	for age in _AGES:
		var engine := _engine()
		var handle := engine._handle
		_grow(engine, age)
		var digest := _micros_per_call(handle.episode_digest)
		var report := _micros_per_call(handle.episode)
		var copy := _micros_per_call(
			func() -> void:
				var _c: Dictionary = engine._episode.duplicate(true),
		)
		print(
			"[observer] %11d   %9.3f   %9.3f   %20.3f   %13.1f" % [
				age,
				digest,
				report,
				copy,
				report / maxf(digest, 0.001),
			],
		)
		# At 60 frames a second, three readers each paying the report column is
		# the whole frame budget question, so the row states it directly.
		print(
			"[observer]               three readers per frame at 60 Hz: "
			+ "%.2f ms/s digest, %.2f ms/s report" % [
				digest * 3.0 * 60.0 / 1000.0,
				report * 3.0 * 60.0 / 1000.0,
			],
		)

	assert_bool(true).is_true()
