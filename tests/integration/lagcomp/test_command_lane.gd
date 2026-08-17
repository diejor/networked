## Law suite for the owner command lane's round trip: the transitions a
## predicting client authored are the transitions its authority replays, under
## the labels the client gave them.
##
## The lane re-sends an overlapping window every frame, so the claim is not that
## the two agree eventually. It is that the consumer's decoded window never
## disagrees with the owner's tape about a transition both hold.
class_name TestCommandLane
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }
const FRAMES := 40


func test_the_consumer_replays_the_transitions_its_owner_authored() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity(
		[&"position"],
		[&"motion", &"bombing"],
		PredictionComponent.MissingInput.STALL,
		0.01,
		PredictionComponent.Schedule.FRAME,
	)
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run_frames(FRAMES)

	var authored := _tape_by_transition(p.client_prediction)
	var decoded := _tape_by_transition(p.server_prediction)
	assert_int(decoded.size()).override_failure_message(
		"a consuming peer that decoded nothing proves nothing about the lane",
	).is_greater(0)

	for transition: int in decoded:
		if not authored.has(transition):
			continue
		var owner_entry: Dictionary = authored[transition]
		var replayed: Dictionary = decoded[transition]
		assert_int(int(replayed["label"])).override_failure_message(
			"transition %d was authored under label %d and replayed under %d"
			% [transition, owner_entry["label"], replayed["label"]],
		).is_equal(int(owner_entry["label"]))
		# A frame the clock held carries the lane and opens no transition, so
		# freshness is what tells a repeated command from a new one and a
		# consumer that lost it would count a hold as a fresh input.
		assert_bool(bool(replayed["fresh"])).override_failure_message(
			"transition %d was authored fresh=%s and replayed fresh=%s"
			% [transition, owner_entry["fresh"], replayed["fresh"]],
		).is_equal(bool(owner_entry["fresh"]))


func _tape_by_transition(
		handle: NetwPredictionHandle,
) -> Dictionary[int, Dictionary]:
	var out: Dictionary[int, Dictionary] = { }
	for entry: Dictionary in handle.tape_transitions():
		out[int(entry["index"])] = entry
	return out
