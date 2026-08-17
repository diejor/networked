## Laws for the seam funnel, which is what a seam's answer passes through
## before anyone reads it.
##
## The funnel exists because the boundary lost two protections when the seam
## went native: a coroutine can cross a bound method with nothing to stop it
## ([method NetwMultiplayerCore.is_coroutine] is the check that reaches it), and
## a wrong-typed answer is indistinguishable from a right one to the caller.
## The funnel refuses both, loudly, and the stock answer stands.
class_name TestSeamFunnel
extends NetwTestSuite

const SEAM := &"_sync_admit_frame"
const ROUTE := 7

# A lambda captures its locals by value, so a run driven inside one reports
# through the suite rather than through its own return.
var _last: Answer = null


## What a seam answered, and what the session did with it.
class Answer:
	var settled: Variant = null
	var misuses: int = 0
	var gates: int = 0


class AwaitingSeam extends RefCounted:
	signal never_settles

	func answer() -> Variant:
		await never_settles
		return ERR_UNAUTHORIZED


class MistypedSeam extends RefCounted:
	func answer() -> Variant:
		return "not an error"


class PlainSeam extends RefCounted:
	func answer() -> Variant:
		return ERR_UNAUTHORIZED


## Drives one seam answer through the funnel and reports what the session made
## of it. [param funnelled] false is the plant: the raw answer is taken as the
## verdict, which is what the boundary did before the funnel existed.
func _run(subject: Object, funnelled: bool) -> Answer:
	var core := NetwMultiplayerCore.new()
	var report := Answer.new()
	core.event_arm(true)
	var raw: Variant = subject.call(&"answer")
	if funnelled:
		report.settled = core.seam_settled(
			SEAM,
			NetwMultiplayerCore.GATE_SYNC,
			ROUTE,
			raw,
			ERR_UNAVAILABLE,
		)
	else:
		report.settled = raw
	for row: NetwEvent in core.event_ring(ROUTE):
		if row.event == NetwMultiplayerCore.SEAM_MISUSE:
			report.misuses += 1
		elif row.event == NetwMultiplayerCore.GATE_SYNC:
			report.gates += 1
	_last = report
	return report


## The law: whatever a seam answers, what the session reads is a value of the
## contract's own type, and an answer that was not one is reported.
func _law_no_coroutine_is_read(report: Answer) -> String:
	if report.settled is Object:
		return "the session read an object where the contract names an int"
	if typeof(report.settled) != TYPE_INT:
		return "the session read a %d where the contract names an int" % [
			typeof(report.settled),
		]
	return ""


## Verify an awaiting override is refused loudly and the stock answer stands.
## The refusal has to reach the error channel: a quiet funnel is the coroutine
## trap again, wearing the funnel as camouflage.
func test_an_awaiting_seam_is_refused_and_the_default_stands() -> void:
	await assert_error(
		func() -> void:
			_run(AwaitingSeam.new(), true)
	).is_push_error(GdUnitArgumentMatchers.any())
	var report := _last

	assert_str(_law_no_coroutine_is_read(report)).is_empty()
	assert_int(report.settled).is_equal(ERR_UNAVAILABLE)
	assert_int(report.misuses).is_equal(1)


## Verify the refusal is what the law reads, by taking the same answer the way
## the boundary took it before the funnel existed.
func test_the_law_reds_when_the_answer_skips_the_funnel() -> void:
	var report := _run(AwaitingSeam.new(), false)

	assert_str(_law_no_coroutine_is_read(report)).is_not_empty()


## Verify a wrong-typed answer is refused on the same terms, since a caller
## cannot tell one from a right one either.
func test_a_mistyped_seam_is_refused() -> void:
	await assert_error(
		func() -> void:
			_run(MistypedSeam.new(), true)
	).is_push_error(GdUnitArgumentMatchers.any())
	var report := _last

	assert_str(_law_no_coroutine_is_read(report)).is_empty()
	assert_int(report.settled).is_equal(ERR_UNAVAILABLE)
	assert_int(report.misuses).is_equal(1)


## Verify an ordinary answer passes through untouched and is reported as the
## stage event rather than as a misuse.
func test_a_plain_seam_passes_through_and_reports_its_stage() -> void:
	var report := _run(PlainSeam.new(), true)

	assert_str(_law_no_coroutine_is_read(report)).is_empty()
	assert_int(report.settled).is_equal(ERR_UNAUTHORIZED)
	assert_int(report.misuses).is_equal(0)
	assert_int(report.gates).is_equal(1)
