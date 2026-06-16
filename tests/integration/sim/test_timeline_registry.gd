## Integration tests for the [LagCompensationService] timeline registry + recorder.
##
## Proves Decision 3 / 4: an entity with a server-authoritative [StateSynchronizer]
## and no [PredictionComponent] is registered by state-sync presence alone, and the
## server records its authoritative snapshot every tick. A non-predicted entity is
## therefore rewindable by default, which is the substrate the server rewind query
## reads. Driven server-only through [RewindScenario].
class_name TestTimelineRegistry
extends NetwTestSuite


func test_state_sync_presence_registers_a_timeline() -> void:
	var s := RewindScenario.new()
	await s.setup(self)
	var entity := await s.spawn_state_entity("Platform")

	# Registered by the StateSynchronizer alone, with no PredictionComponent.
	var tl := s.sim.timeline_of(entity)
	assert_bool(tl != null).is_true()
	# The registry timeline is published to the entity slot.
	assert_bool(tl == entity.timeline).is_true()
	# Registration is idempotent: a repeat call returns the same instance.
	assert_bool(s.sim.register_timeline(entity) == tl).is_true()


func test_server_records_authoritative_state_each_tick() -> void:
	var s := RewindScenario.new()
	await s.setup(self)
	var entity := await s.spawn_state_entity("Platform")

	# Move the body right one step at a time so each tick records a fresh
	# authoritative position into the timeline.
	var live := s.move_along(entity, func(i: int) -> Vector2: return Vector2(float(i) * 5.0, 0.0), 24)

	var view_tick: int = s.clock.tick - 8
	var rewound: Dictionary = entity.timeline.latest_state_at_or_before(view_tick)

	# The non-predicted entity has usable rewind history.
	assert_bool(rewound.is_empty()).is_false()
	# The history reflects where the target was, not where it is now.
	var rewound_pos: Vector2 = rewound[&"position"]
	assert_bool(rewound_pos.x < live.x).is_true()


func test_unregister_drops_the_timeline() -> void:
	var s := RewindScenario.new()
	await s.setup(self)
	var entity := await s.spawn_state_entity("Platform")
	assert_bool(s.sim.timeline_of(entity) != null).is_true()

	s.sim.unregister_timeline(entity)
	assert_bool(s.sim.timeline_of(entity) == null).is_true()
