## Laws for the settle queue, the shell-owned effect queue drained at the pump.
##
## A core that needed an effect to land "after the cascade that scheduled it"
## used to say so with [code]call_deferred[/code], which puts the effect on the
## engine's queue and makes its cadence "whenever the host next iterates". That
## is not a contract, it is unobservable from a tier that cannot flush the
## message queue, and it does not survive a core moving to C++. The settle queue
## is the replacement, and these are the properties it has to have before any
## site is allowed to depend on it.
class_name TestSettleQueue
extends NetwTestSuite

var api: NetwMultiplayer
var seen: Array[StringName] = []


func before_test() -> void:
	api = NetwMultiplayer.make(SceneMultiplayer.new())
	seen = []


func after_test() -> void:
	api.embedding.dispose()
	api = null
	super.after_test()


func _note(name: StringName) -> void:
	seen.append(name)


## An unkeyed effect runs once, at the drain, in the order it was scheduled.
func test_unkeyed_effects_run_in_enqueue_order() -> void:
	api._settle_schedule(_note.bind(&"first"))
	api._settle_schedule(_note.bind(&"second"))
	assert_array(seen).is_empty()

	api._settle()

	assert_array(seen).is_equal([&"first", &"second"] as Array[StringName])


## A key coalesces to the LATEST position, because every site that schedules one
## means "run after the cascade that scheduled me" rather than "run where I was
## first asked for".
func test_a_key_coalesces_to_the_latest_position() -> void:
	api._settle_schedule(_note.bind(&"keyed"), &"k")
	api._settle_schedule(_note.bind(&"unkeyed"))
	api._settle_schedule(_note.bind(&"keyed"), &"k")

	api._settle()

	assert_array(seen).is_equal([&"unkeyed", &"keyed"] as Array[StringName])


## A caller that did the work on the spot withdraws its pending effect, which is
## how a flush_now leaves nothing behind for the next pump to repeat.
func test_cancel_withdraws_a_pending_key() -> void:
	api._settle_schedule(_note.bind(&"keyed"), &"k")
	api._settle_cancel(&"k")

	api._settle()

	assert_array(seen).is_empty()


## An effect that schedules an effect is legal, and the new one runs in the SAME
## drain. Otherwise a site would have to know whether its own cascade had
## already been drained, which is the reasoning the queue exists to remove.
func test_an_effect_scheduled_during_a_drain_runs_in_the_same_drain() -> void:
	api._settle_schedule(
		func() -> void:
			_note(&"outer")
			api._settle_schedule(_note.bind(&"inner")),
	)

	api._settle()

	assert_array(seen).is_equal([&"outer", &"inner"] as Array[StringName])


## The miswiring probe. An effect that reschedules itself forever is a defect,
## and the one shape it must never take is a hang: the drain is bounded, names
## the keys still pending, and returns.
func test_a_self_rescheduling_effect_fails_loudly_rather_than_hanging() -> void:
	api._settle_schedule(_reschedule_forever, &"cycle")

	await assert_error(
		func() -> void:
			api._settle()
	).is_push_error(GdUnitArgumentMatchers.any())

	# Bounded, so the count is the bound rather than whatever the machine had
	# time for, and the queue is left empty rather than poisoned for the next
	# pump.
	assert_int(seen.size()).is_equal(NetwMultiplayer._SETTLE_PASSES)
	assert_array(api._settle_queue).is_empty()


# The miswiring: an effect whose own body puts it back on the queue under the
# same key, which coalescing cannot save because there is nothing to coalesce
# with.
func _reschedule_forever() -> void:
	_note(&"pass")
	api._settle_schedule(_reschedule_forever, &"cycle")


## The queue is empty after a drain, so a second pump with no new work is a
## no-op rather than a repeat.
func test_a_drained_queue_stays_drained() -> void:
	api._settle_schedule(_note.bind(&"once"))
	api._settle()
	api._settle()

	assert_array(seen).is_equal([&"once"] as Array[StringName])
