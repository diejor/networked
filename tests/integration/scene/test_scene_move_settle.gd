## The arrivals a scene move reports, observed with no frame.
##
## [method NetwSceneHandle.move_participants] reports each arrival onto the
## group promise it returns, and it cannot report on the spot: a caller chaining
## [method NetwGroupPromise.then] is not subscribed until the call returns.
## These cases pin the report to the session's own settle, with nothing awaited
## and no frame driven between the move and the assertion, and pin it unkeyed,
## because two moves in one cascade are two batches rather than one.
class_name TestSceneMoveSettle
extends NetwTestSuite

var harness: NetwTestHarness
var source: NetwSceneHandle
var dest: NetwSceneHandle
var participant: NetwParticipant


func before_test() -> void:
	harness = make_unmanaged_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)

	var source_builder := LevelBuilder.new("MoveSettleSource").with_root(Node2D)
	source_builder.pack()
	var dest_builder := LevelBuilder.new("MoveSettleDest").with_root(Node2D)
	dest_builder.pack()
	harness.register_spawnable_scene(source_builder.packed)
	harness.register_spawnable_scene(dest_builder.packed)

	var client := await harness.add_client()
	participant = harness.server().api.peer_get_participant(
		client.multiplayer_peer.get_unique_id(),
	)
	source = harness.scene_on_server(source_builder.scene_name)
	dest = harness.scene_on_server(dest_builder.scene_name)
	source.admit(participant)


func after_test() -> void:
	await harness.teardown()
	await super.after_test()


## The move leaves the group unsettled so a caller can subscribe, and the
## session's next pump is what settles it.
func test_a_move_settles_its_group_at_the_pump() -> void:
	var batch := dest.move_participants([participant])
	var arrivals: Array[int] = []
	batch.completed_single.connect(
		func(peer: int, _p: NetwParticipant) -> void: arrivals.append(peer)
	)

	assert_bool(batch.is_completed).override_failure_message(
		"a group settled before the caller returns cannot be subscribed to",
	).is_false()

	harness.server().api.poll()

	assert_bool(batch.is_completed).is_true()
	assert_array(arrivals).contains_exactly([participant.peer_id])


## A move of nobody has no arrival to settle it, so the report settles it, and
## at the same pump every other move settles at.
func test_a_move_of_nobody_still_settles_at_the_pump() -> void:
	var batch := dest.move_participants([])
	var completions: Array[int] = []
	batch.completed.connect(
		func(results: Dictionary) -> void: completions.append(results.size())
	)

	assert_bool(batch.is_completed).override_failure_message(
		"an empty group settled before the caller returns cannot be awaited",
	).is_false()

	harness.server().api.poll()

	assert_bool(batch.is_completed).is_true()
	assert_array(completions).contains_exactly([0])


## Two moves in one cascade settle two groups, which is what leaving the report
## unkeyed buys. A shared key would coalesce them and strand the first.
func test_two_moves_in_one_cascade_each_settle() -> void:
	var back := source.move_participants([participant])
	var forward := dest.move_participants([participant])

	harness.server().api.poll()

	assert_bool(back.is_completed).override_failure_message(
		"the first move must not lose its report to the second",
	).is_true()
	assert_bool(forward.is_completed).is_true()
