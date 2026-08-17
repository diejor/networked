## Law suite for what a watched session is told about one entity's life: it goes
## live, its teardown opens, and it dies, in that order and on its own route.
##
## The death is the one edge that can answer nothing, because a route outlives
## the entity that held it by exactly one event. That is why the teardown edge
## carries the model, and why this suite reads its rows through a sink rather
## than through the ring: a terminal event drops the route's history with it.
class_name TestEntityLifeReports
extends NetwTestSuite

var harness: NetwTestHarness
var player_builder: PlayerBuilder
var level_builder: LevelBuilder

# Every lifecycle row the server reported, in the order the plane delivered it.
var _reported: Array[NetwEvent] = []


func before_test() -> void:
	_reported = []
	player_builder = PlayerBuilder.new("LifeReportPlayer") \
			.with_root(StateSyncBody) \
			.with_multiplayer_entity() \
			.with_state([&"position"])
	player_builder.pack()

	level_builder = LevelBuilder.new("LifeReportLevel") \
			.with_root(Node2D) \
			.with_multiplayer_spawner("..", [player_builder.packed])
	level_builder.pack()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	harness = null

	player_builder = null
	level_builder = null

	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func test_a_life_reports_its_spawn_its_teardown_and_its_death() -> void:
	var player := await _watched_player()
	var entity := NetwEntity.of(player)
	var route := entity.route
	var entity_id := entity.entity_id
	var peer_id := entity.peer_id

	entity.despawn(NetwDespawnOpts.create(&"law"))
	await player.tree_exited
	await NetwTestSuite.drain_frames(get_tree(), 2)

	# The route goes live while the node is still entering the tree, so the
	# session reports a spawned route before the entity reports the
	# materialization that asked for one.
	var life := _rows_on(route).filter(
		func(row: NetwEvent) -> bool:
			return row.event != NetwMultiplayerCore.STAGE_TRANSITION
	)
	assert_array(life.map(func(row: NetwEvent) -> int: return row.event)).is_equal(
		[
			NetwMultiplayerCore.SPAWNED,
			NetwMultiplayerCore.SPAWNING,
			NetwMultiplayerCore.DESPAWNING,
			NetwMultiplayerCore.DESPAWNED,
		]
	)

	var materialized := _row_of(life, NetwMultiplayerCore.SPAWNING)
	assert_object(materialized).is_not_null()
	assert_str(str(materialized.entity_id)).is_equal(str(entity_id))

	var spawned := _row_of(life, NetwMultiplayerCore.SPAWNED)
	assert_object(spawned).is_not_null()
	assert_str(str(spawned.entity_id)).is_equal(str(entity_id))
	assert_int(spawned.peer).is_equal(peer_id)

	var teardown := _row_of(life, NetwMultiplayerCore.DESPAWNING)
	assert_object(teardown).is_not_null()
	assert_str(str(teardown.detail.get(&"reason"))).is_equal("law")

	# The death answers about an entity nothing can be asked about any more.
	var death := _row_of(life, NetwMultiplayerCore.DESPAWNED)
	assert_object(death).is_not_null()
	assert_str(str(death.model.get(&"entity_id"))).is_equal(str(entity_id))
	assert_int(int(death.model.get(&"peer_id"))).is_equal(peer_id)


func test_a_life_reports_every_stage_it_passes_through() -> void:
	var player := await _watched_player()
	var entity := NetwEntity.of(player)
	var route := entity.route
	var entity_id := entity.entity_id

	entity.despawn(NetwDespawnOpts.create(&"law"))
	await player.tree_exited
	await NetwTestSuite.drain_frames(get_tree(), 2)

	# An entity is armed before a route is allocated for it, so the edge into
	# ARMED is a row the session carries rather than one the route does.
	var armed := _stages_on(0).filter(
		func(row: NetwEvent) -> bool:
			return str(row.entity_id) == str(entity_id)
	)
	assert_int(armed.size()).is_equal(1)
	assert_int(int(armed[0].detail.get(&"from"))).is_equal(
		NetwEntity.STAGE_UNBOUND
	)
	assert_int(int(armed[0].detail.get(&"to"))).is_equal(NetwEntity.STAGE_ARMED)

	var ladder := _stages_on(route)
	assert_array(
		ladder.map(func(row: NetwEvent) -> int: return int(row.detail.get(&"to")))
	).is_equal(
		[
			NetwEntity.STAGE_LIVE,
			NetwEntity.STAGE_DESPAWNING,
			NetwEntity.STAGE_FREED,
		]
	)
	assert_int(int(ladder[0].detail.get(&"from"))).is_equal(
		NetwEntity.STAGE_ARMED
	)


func test_a_watcher_reads_the_state_an_event_does_not_carry() -> void:
	var player := await _watched_player()
	var entity := NetwEntity.of(player)
	var route := entity.route
	var entity_id := entity.entity_id
	var core: NetwMultiplayerCore = harness.server().api._native_core

	var described := core.entity_describe(route)
	assert_int(int(described.get(&"route", -1))).is_equal(route)
	assert_str(str(described.get(&"entity_id", ""))).is_equal(str(entity_id))
	assert_int(int(described.get(&"peer_id", -1))).is_equal(entity.peer_id)
	assert_int(int(described.get(&"stage", -1))).is_equal(NetwEntity.STAGE_LIVE)
	assert_int(int(described.get(&"controller", -1))).is_equal(entity.controller)
	assert_int(int(described.get(&"liveness", -1))).is_equal(
		NetwLivenessCore.STATE_LIVE
	)
	assert_bool(described.has(&"layers")).is_true()

	assert_dict(core.entity_describe(route + 1000)).is_empty()

	entity.despawn(NetwDespawnOpts.create(&"law"))
	await player.tree_exited
	await NetwTestSuite.drain_frames(get_tree(), 2)

	# The read dies with its subject, which is why the death hands on a model
	# rather than a route to ask about.
	assert_dict(core.entity_describe(route)).is_empty()
	var death := _row_of(_rows_on(route), NetwMultiplayerCore.DESPAWNED)
	assert_object(death).is_not_null()
	assert_str(str(death.model.get(&"entity_id"))).is_equal(str(entity_id))


func test_a_settled_move_reports_the_reason_that_asked_for_it() -> void:
	var player := await _watched_player()
	var entity := NetwEntity.of(player)
	var route := entity.route

	var holder := Node2D.new()
	holder.name = "Holder"
	player.get_parent().add_child(holder)
	var opts := NetwReparentOpts.new()
	opts.reason = &"law_move"
	entity.reparent_to(holder, opts)
	await NetwTestSuite.drain_frames(get_tree(), 3)

	var moves := _rows_on(route).filter(
		func(row: NetwEvent) -> bool:
			return row.event == NetwMultiplayerCore.REPARENTED
	)
	assert_int(moves.size()).is_equal(1)
	assert_str(str(moves[0].detail.get(&"reason"))).is_equal("law_move")
	assert_bool(bool(moves[0].detail.get(&"moved"))).is_false()


# A server with a joined client, watching every lifecycle row it reports, and
# one player standing in the level. The watch is installed before the spawn,
# because a row nobody was watching for was never recorded.
func _watched_player() -> Node:
	harness = make_harness()
	var sm_factory := func() -> MultiplayerSceneManager:
		var sm := NetwTestSuite.create_scene_manager()
		sm.register_initial_scene_path(level_builder.resource_path)
		sm.register_scene_path(player_builder.resource_path)
		return sm
	await harness.setup_factory(sm_factory)

	var client := await harness.add_client("jose")
	harness.server().api._native_core.event_watch(
		[
			NetwMultiplayerCore.SPAWNING,
			NetwMultiplayerCore.SPAWNED,
			NetwMultiplayerCore.DESPAWNING,
			NetwMultiplayerCore.DESPAWNED,
			NetwMultiplayerCore.REPARENTED,
			NetwMultiplayerCore.STAGE_TRANSITION,
		],
		{ },
		{ },
		_on_event,
	)

	var player := harness.spawn_player(client, player_builder.packed)
	await harness.wait_for_player(client, level_builder.scene_name)
	return player


# The rows one route collected, which is what tells a player's life from the
# level container's, since both are entities and both report.
func _rows_on(route: int) -> Array[NetwEvent]:
	var kept: Array[NetwEvent] = []
	for row in _reported:
		if row.route == route:
			kept.append(row)
	return kept


func _stages_on(route: int) -> Array[NetwEvent]:
	return _rows_on(route).filter(
		func(row: NetwEvent) -> bool:
			return row.event == NetwMultiplayerCore.STAGE_TRANSITION
	)


func _row_of(rows: Array[NetwEvent], event: int) -> NetwEvent:
	for row in rows:
		if row.event == event:
			return row
	return null


func _on_event(event: NetwEvent) -> void:
	_reported.append(event)
