## Integration tests for the [MultiplayerSimulation] timeline registry + recorder.
##
## Proves Decision 3 / 4: an entity with a server-authoritative [StateSynchronizer]
## and no [PredictionComponent] is registered by state-sync presence alone, and the
## server records its authoritative snapshot every tick. A non-predicted entity is
## therefore rewindable by default, which is the substrate the server rewind query
## reads. Driven server-only through a single-clock [LockstepStepper].
class_name TestTimelineRegistry
extends NetwTestSuite

var _harness: NetwTestHarness
var _server: MultiplayerTree
var _server_clock: MultiplayerClock
var _sim: MultiplayerSimulation


func _pos(i: int) -> Vector2:
	return Vector2(float(i) * 5.0, 0.0)


# Server tree, hosted, with a clock and a MultiplayerSimulation bound to it.
func _setup_server() -> void:
	_harness = make_harness()
	await _harness.setup()
	await _harness.host_server()
	_server = _harness.server()
	_server_clock = await _harness.add_clock(30, 3)

	_sim = MultiplayerSimulation.new()
	_sim.name = "MultiplayerSimulation"
	_server.add_child(_sim)
	await (Engine.get_main_loop() as SceneTree).process_frame


# A server-authoritative state-synced entity, no prediction component.
func _spawn_state_entity(entity_name: String) -> NetwEntity:
	var node := Node2D.new()
	node.name = entity_name
	var entity := NetwEntity.ensure(node)

	var state := StateSynchronizer.new()
	state.name = "StateSync"
	state.register_property(
		&"position",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		false,
		true,
	)
	node.add_child(state)
	state.owner = node
	state.root_path = state.get_path_to(node)

	_server.add_child(node)
	await (Engine.get_main_loop() as SceneTree).process_frame
	return entity


func test_state_sync_presence_registers_a_timeline() -> void:
	await _setup_server()
	var entity := await _spawn_state_entity("Platform")

	# Registered by the StateSynchronizer alone, with no PredictionComponent.
	var tl := _sim.timeline_of(entity)
	assert_bool(tl != null).is_true()
	# The registry timeline is published to the entity slot.
	assert_bool(tl == entity.timeline).is_true()
	# Registration is idempotent: a repeat call returns the same instance.
	assert_bool(_sim.register_timeline(entity) == tl).is_true()


func test_server_records_authoritative_state_each_tick() -> void:
	await _setup_server()
	var entity := await _spawn_state_entity("Platform")
	var node := entity.owner as Node2D

	var stepper := LockstepStepper.new(
		[_server_clock] as Array[MultiplayerClock],
		[_server.multiplayer] as Array[MultiplayerAPI],
		_harness.session(),
		30,
	)

	# Move the body right one step at a time so each tick records a fresh
	# authoritative position into the timeline.
	for i in range(24):
		node.position = _pos(i)
		stepper.sync_ticks(1)

	var live: Vector2 = node.position
	var view_tick: int = _server_clock.tick - 8
	var rewound: Dictionary = entity.timeline.latest_state_at_or_before(view_tick)

	# The non-predicted entity has usable rewind history.
	assert_bool(rewound.is_empty()).is_false()
	# The history reflects where the target was, not where it is now.
	var rewound_pos: Vector2 = rewound[&"position"]
	assert_bool(rewound_pos.x < live.x).is_true()


func test_unregister_drops_the_timeline() -> void:
	await _setup_server()
	var entity := await _spawn_state_entity("Platform")
	assert_bool(_sim.timeline_of(entity) != null).is_true()

	_sim.unregister_timeline(entity)
	assert_bool(_sim.timeline_of(entity) == null).is_true()
