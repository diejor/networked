## Integration tests for scoped scene rewind ([method NetwLagCompensation.rewind]).
##
## Scene rewind briefly applies an entity's recorded past state to its live node so
## validation code can read the live node where the shooter saw the target, then
## restores it. Proves the contract bomber relies on: inside the callable a shot
## aimed at the perceived (past) position hits the live body while a shot at the
## current position misses, and the live state is restored unconditionally on
## return. Driven server-only through a single-clock [LockstepStepper].
class_name TestSceneRewind
extends NetwTestSuite

const RADIUS := 6.0

var _harness: NetwTestHarness
var _server: MultiplayerTree
var _server_clock: MultiplayerClock


func _hits(aim: Vector2, target: Vector2) -> bool:
	return aim.distance_to(target) <= RADIUS


func _setup_server() -> void:
	_harness = make_harness()
	await _harness.setup()
	await _harness.host_server()
	_server = _harness.server()
	_server_clock = await _harness.add_clock(30, 3)
	await (Engine.get_main_loop() as SceneTree).process_frame


# A server-authoritative state-synced entity whose Node2D root carries the
# rewound transform.
func _spawn_state_entity() -> NetwEntity:
	var node := PlayerBuilder.new("Target") \
			.with_root(Node2D) \
			.with_state([&"position"]) \
			.build()
	_server.add_child(node)
	await (Engine.get_main_loop() as SceneTree).process_frame
	return NetwEntity.of(node)


func test_rewind_moves_live_node_to_history_then_restores() -> void:
	await _setup_server()
	var entity := await _spawn_state_entity()
	var node := entity.owner as Node2D

	var stepper := LockstepStepper.new(
		[_server_clock] as Array[MultiplayerClock],
		[_server.multiplayer] as Array[MultiplayerAPI],
		_harness.session(),
		30,
	)

	# Move the body steadily right, recording a fresh authoritative position each tick.
	for i in range(24):
		node.position = Vector2(float(i) * 8.0, 0.0)
		stepper.sync_ticks(1)

	var live: Vector2 = node.position
	var view_tick: int = _server_clock.tick - 8
	var perceived: Vector2 = _server.lag_compensation.sample(entity, view_tick).position
	var targets: Array[NetwEntity] = [entity]

	# A Dictionary collects the in-callable reads: a GDScript lambda captures locals
	# by value, so assignments to plain locals inside it would not escape.
	var probe := { &"seen": Vector2.ZERO, &"hit_perceived": false, &"hit_live": false }
	_server.lag_compensation.rewind(targets, view_tick, func() -> void:
		# Inside the callable the live node holds its perceived (past) position.
		probe[&"seen"] = node.position
		probe[&"hit_perceived"] = _hits(node.position, perceived)
		probe[&"hit_live"] = _hits(node.position, live)
	)

	# The body moved between the perceived tick and now.
	assert_that(perceived.distance_to(live)).is_greater(RADIUS)
	# Inside the callable the live node was rewound to the perceived position.
	assert_vector(probe[&"seen"]).is_equal_approx(perceived, Vector2.ONE * 0.001)
	# A shot aimed where the shooter saw the target hits; aimed at the live spot misses.
	assert_bool(probe[&"hit_perceived"]).is_true()
	assert_bool(probe[&"hit_live"]).is_false()
	# The live state is restored unconditionally on return.
	assert_vector(node.position).is_equal_approx(live, Vector2.ONE * 0.001)


func test_rewind_skips_entity_with_no_history() -> void:
	await _setup_server()
	var entity := await _spawn_state_entity()
	var node := entity.owner as Node2D
	node.position = Vector2(42.0, 7.0)
	var targets: Array[NetwEntity] = [entity]

	var probe := { &"ran": false }
	# A view tick with no retained state leaves the live node untouched, but still
	# runs the callable (it simply has no rewound targets to read).
	_server.lag_compensation.rewind(targets, -100, func() -> void:
		probe[&"ran"] = true
	)

	assert_bool(probe[&"ran"]).is_true()
	assert_vector(node.position).is_equal_approx(Vector2(42.0, 7.0), Vector2.ONE * 0.001)


# A server-authoritative state-synced entity that can be despawned with linger.
func _spawn_despawnable_entity() -> Node2D:
	var node := PlayerBuilder.new("Linger") \
			.with_root(Node2D) \
			.with_multiplayer_entity() \
			.with_state([&"position"]) \
			.build()
	_server.add_child(node)
	await (Engine.get_main_loop() as SceneTree).process_frame
	return node as Node2D


func test_linger_keeps_target_rewindable_until_freed() -> void:
	await _setup_server()
	var node := await _spawn_despawnable_entity()
	var entity := NetwEntity.of(node)

	# Registered by StateSynchronizer presence on the server. Seed a known
	# authoritative state so the rewind query is meaningful after despawn.
	var tl := _server.lag_compensation.timeline_of(entity)
	assert_that(tl).is_not_null()
	tl.record_state(5, { &"position": Vector2(40.0, 0.0) })

	# Despawn with linger: the target dies but stays rewindable for the window.
	var opts := DespawnOpts.new(&"killed")
	opts.linger = true
	opts.linger_seconds = 0.15
	MultiplayerEntity.unwrap(node).despawn(opts)
	await (Engine.get_main_loop() as SceneTree).process_frame

	# During the window the entity lingers (deactivated, not freed) and stays
	# rewindable, so a late shooter still validates against where the target was.
	assert_bool(is_instance_valid(node)).is_true()
	assert_that(_server.lag_compensation.timeline_of(entity)).is_not_null()
	var during := _server.lag_compensation.sample(entity, 5)
	assert_vector(during.position).is_equal_approx(Vector2(40.0, 0.0), Vector2.ONE * 0.001)

	# After the window passes the entity frees and its timeline unregisters.
	await (Engine.get_main_loop() as SceneTree).create_timer(0.35).timeout
	await (Engine.get_main_loop() as SceneTree).process_frame
	assert_bool(is_instance_valid(node)).is_false()
	assert_that(_server.lag_compensation.timeline_of(entity)).is_null()
	assert_bool(_server.lag_compensation.sample(entity, 5).is_empty()).is_true()
