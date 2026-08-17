## Real-node server-side rewind (ports the lag-comp spike tier E).
##
## With the server recording authoritative snapshots into the per-entity
## [NetwTimeline] registry each tick, lag compensation is a query, not a system:
## sampling the timeline at the shooter's perceived tick reproduces where the
## target was, so a late hit registers against the rewound position and misses
## against the live one. Exercises the public seam
## [method NetwMultiplayer.lagcomp_sample].
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

	var view_tick: int = s.server_clock.tick - 8
	var past := s.server.api.lagcomp_sample(
		s.server.api.entity_of(p.server_root),
		view_tick,
	)
	assert_that(past.is_empty()).is_false()
	var rewound_pos: Vector2 = past.get_value(&"position")
	var live_pos: Vector2 = p.server_body.position

	# The target moved between the perceived tick and now.
	assert_that(rewound_pos.distance_to(live_pos)).is_greater(RADIUS)

	# A shot aimed where the shooter saw the target hits the rewound history and
	# misses the live body.
	assert_that(_hits(rewound_pos, rewound_pos)).is_true()
	assert_that(_hits(rewound_pos, live_pos)).is_false()

