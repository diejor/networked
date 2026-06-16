## Real-node server-side rewind (ports the lag-comp spike tier E).
##
## With the server recording authoritative snapshots into the per-entity
## [NetwTimeline] registry each tick, lag compensation is a query, not a system:
## sampling the timeline at the shooter's perceived tick reproduces where the
## target was, so a late hit registers against the rewound position and misses
## against the live one. Reads the real registry through
## [method MultiplayerSimulation.timeline_of], the last consumer the spike
## doubles fed.
class_name TestServerRewind
extends NetwTestSuite

const RADIUS := 6.0
const RIGHT := { &"motion": Vector2.RIGHT }


func _hits(aim: Vector2, target: Vector2) -> bool:
	return aim.distance_to(target) <= RADIUS


func test_sample_hits_rewound_misses_live() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(60)

	var tl := s.server_sim.timeline_of(p.server_entity)
	assert_that(tl).is_not_null()
	var view_tick: int = s.server_clock.tick - 8
	var rewound: Dictionary = tl.latest_state_at_or_before(view_tick)
	assert_that(rewound.is_empty()).is_false()
	var rewound_pos: Vector2 = rewound[&"position"]
	var live_pos: Vector2 = p.server_body.position

	# The target moved between the perceived tick and now.
	assert_that(rewound_pos.distance_to(live_pos)).is_greater(RADIUS)

	# A shot aimed where the shooter saw the target hits the rewound history and
	# misses the live body.
	assert_that(_hits(rewound_pos, rewound_pos)).is_true()
	assert_that(_hits(rewound_pos, live_pos)).is_false()


func test_clamps_view_tick_into_retained_window() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(40)

	# A view tick older than anything retained returns neutral, the signal a real
	# compensator clamps against rather than fabricating a position.
	var tl := s.server_sim.timeline_of(p.server_entity)
	var ancient: Dictionary = tl.latest_state_at_or_before(-100)
	assert_that(ancient.is_empty()).is_true()


func test_despawn_linger_keeps_target_rewindable() -> void:
	# Models the despawn linger: the timeline outlives the node's active life
	# until the history window passes the despawn tick.
	var tl := NetwTimeline.new()
	for t in range(0, 30):
		tl.record_state(t, { &"position": Vector2(t, 0) })
	var despawn_tick := 29

	# A shooter who saw the target at tick 20 can still validate after despawn.
	var sample: Dictionary = tl.latest_state_at_or_before(20)
	assert_that(sample.is_empty()).is_false()
	assert_that(sample[&"position"]).is_equal(Vector2(20, 0))

	# Once the retained window passes the despawn tick, the entry expires.
	tl.trim_before(despawn_tick + 1)
	assert_that(tl.latest_state_at_or_before(20).is_empty()).is_true()
