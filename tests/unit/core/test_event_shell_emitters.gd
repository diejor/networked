## Laws for the facts a session reports from GDScript, which is every fact whose
## owner has not crossed into the native core yet.
##
## The completeness claim is the same one the native emitters answer: an act the
## session performed leaves one row of its own kind, and an act it did not
## perform leaves none. The lifecycle group carries a second claim the others do
## not, because its terminal event is the one stop that cannot query anything:
## [constant NetwMultiplayerCore.DESPAWNED] hands on the model snapshotted at
## [constant NetwMultiplayerCore.DESPAWNING], which fired while the entity was
## still whole.
class_name TestEventShellEmitters
extends NetwTestSuite

const ROUTE := 12

# What a sink was handed, which is the only place a terminal model can be read:
# the ring is dropped with the subject.
var _stopped_on: NetwEvent = null


## The rows a run drained, by taxonomy value.
func _counts(api: NetwMultiplayer, route: int) -> Dictionary:
	var seen: Dictionary[int, int] = { }
	for row: NetwEvent in api._native_core.event_ring(route):
		seen[row.event] = int(seen.get(row.event, 0)) + 1
	return seen


# The suite instance outlives one case, so a sink's record would otherwise
# carry into the next case and be read as that case's own.
func before_test() -> void:
	_stopped_on = null


func _armed_session() -> NetwMultiplayer:
	var api := NetwMultiplayer.new()
	api._native_core.event_arm(true)
	return api


## Verify a peer edge the shell owns is reported, with the peer on the record.
func test_a_peer_edge_reports_its_peer() -> void:
	var api := _armed_session()

	api.report_event(NetwMultiplayerCore.PEER_JOINED, 0, { peer = 9 }, 9)

	var rows := api._native_core.event_ring(0)
	assert_int(rows.size()).is_equal(1)
	var row: NetwEvent = rows[0]
	assert_int(row.event).is_equal(NetwMultiplayerCore.PEER_JOINED)
	assert_int(row.peer).is_equal(9)
	assert_str(row.get_event_name()).is_equal("PEER_JOINED")


## Verify a stage's own answer reaches the record it reports, since a stage that
## judged something says so without counting it: the count belongs to the
## session's hostile-input book and a local stage's refusal is not remote input.
func test_a_reported_stage_carries_the_verdict_it_answered() -> void:
	var api := _armed_session()

	api.report_event(
		NetwMultiplayerCore.SYNC_DECODE,
		ROUTE,
		{ comp = 2 },
		0,
		&"",
		{ },
		ERR_INVALID_DATA,
	)

	var rows := api._native_core.event_ring(ROUTE)
	assert_int(rows.size()).is_equal(1)
	var row: NetwEvent = rows[0]
	assert_int(row.verdict).is_equal(ERR_INVALID_DATA)
	assert_int(api._native_core.verdict_total(ERR_INVALID_DATA)).is_equal(0)


## Verify a session nobody watches records nothing, which is what the pre-check
## in [method NetwMultiplayer.report_event] buys.
func test_an_unwatched_session_records_nothing() -> void:
	var api := NetwMultiplayer.new()

	api.report_event(NetwMultiplayerCore.PEER_JOINED, 0, { peer = 9 }, 9)

	assert_int(api._native_core.event_ring(0).size()).is_equal(0)


## Verify the terminal event carries the model its subject last had, which is
## the whole reason the lingering edge reports one.
func test_a_death_carries_the_model_the_entity_last_had() -> void:
	var api := _armed_session()
	api._native_core.event_watch(
		[NetwMultiplayerCore.DESPAWNED],
		{ route = ROUTE },
		{ },
		_on_event,
	)

	api.report_event(
		NetwMultiplayerCore.DESPAWNING,
		ROUTE,
		{ peer_id = 3 },
		3,
		&"Racer",
		{ entity_id = &"Racer", peer_id = 3 },
	)
	api.report_event(NetwMultiplayerCore.DESPAWNED, ROUTE)

	assert_object(_stopped_on).is_not_null()
	assert_int(_stopped_on.event).is_equal(NetwMultiplayerCore.DESPAWNED)
	assert_str(str(_stopped_on.model.get(&"entity_id"))).is_equal("Racer")
	assert_int(int(_stopped_on.model.get(&"peer_id"))).is_equal(3)
	# Nothing can follow a death, so the route's history goes with it.
	assert_int(api._native_core.event_ring(ROUTE).size()).is_equal(0)


func _on_event(event: NetwEvent) -> void:
	_stopped_on = event


## Verify a row that names an entity by id sees that entity's whole life,
## including the death. A death knows a route and nothing else, so a row keyed
## by id would otherwise match every event but the one worth stopping on.
func test_a_row_keyed_by_entity_id_sees_the_death() -> void:
	var api := _armed_session()
	api._native_core.event_watch(
		[NetwMultiplayerCore.SPAWNED, NetwMultiplayerCore.DESPAWNED],
		{ entity_id = &"Racer" },
		{ },
		_on_event,
	)

	api.report_event(
		NetwMultiplayerCore.SPAWNED, ROUTE, { }, 3, &"Racer",
	)
	assert_int(_stopped_on.event).is_equal(NetwMultiplayerCore.SPAWNED)
	api.report_event(NetwMultiplayerCore.DESPAWNED, ROUTE)

	assert_int(_stopped_on.event).is_equal(NetwMultiplayerCore.DESPAWNED)
	assert_str(str(_stopped_on.entity_id)).is_equal("Racer")


## Verify the taxonomy is closed at the shell boundary too: a value outside it
## opens no row rather than a row nothing can name.
func test_a_value_outside_the_taxonomy_reports_nothing() -> void:
	var api := _armed_session()

	api.report_event(9, ROUTE)

	assert_int(api._native_core.event_ring(ROUTE).size()).is_equal(0)


## Verify the public face is the session's own, not the core it holds, and that
## a row can name the layer an edge belongs to. A predicate key is refused
## unless the vocabulary holds it, so [code]layer_in[/code] existing is what
## makes "break when this layer gains an observer" expressible at all.
func test_a_row_can_name_the_layer_an_edge_belongs_to() -> void:
	var api := _armed_session()
	var id := api.event_watch(
		[NetwMultiplayerCore.OBSERVER_ENTERED],
		{ },
		{ layer_in = ["combat"] },
		_on_event,
	)
	assert_int(id).is_greater(0)

	api.report_event(
		NetwMultiplayerCore.OBSERVER_ENTERED, ROUTE, { layer = &"ambient" }, 4,
	)
	assert_object(_stopped_on).is_null()
	api.report_event(
		NetwMultiplayerCore.OBSERVER_ENTERED, ROUTE, { layer = &"combat" }, 4,
	)

	assert_object(_stopped_on).is_not_null()
	assert_int(_stopped_on.peer).is_equal(4)
	assert_int(api.event_watches().size()).is_equal(1)
	assert_bool(api.event_unwatch(id)).is_true()


## Verify an unknown predicate key still refuses through the public face, so
## the vocabulary is closed wherever a row is installed from.
func test_an_unknown_predicate_key_refuses_through_the_session() -> void:
	var api := _armed_session()

	await assert_error(
		func() -> void:
			assert_int(
				api.event_watch(
					[NetwMultiplayerCore.OBSERVER_ENTERED],
					{ },
					{ layer_is = "combat" },
					_on_event,
				)
			).is_equal(-1)
	).is_push_error(GdUnitArgumentMatchers.any())


## Verify each shell group leaves one row per act and nothing extra, which is
## the completeness claim extended to the groups the core does not own.
func test_every_shell_group_leaves_one_row_per_act() -> void:
	var api := _armed_session()

	api.report_event(NetwMultiplayerCore.SPAWNED, ROUTE, { peer_id = 3 }, 3)
	api.report_event(NetwMultiplayerCore.SPAWNED, ROUTE, { peer_id = 3 }, 3)
	api.report_event(NetwMultiplayerCore.INTEREST_COMMIT)
	api.report_event(NetwMultiplayerCore.SCENE_LIVE, ROUTE, { scene = "Track" })

	var on_route := _counts(api, ROUTE)
	var on_session := _counts(api, 0)
	assert_int(int(on_route.get(NetwMultiplayerCore.SPAWNED, 0))).is_equal(2)
	assert_int(int(on_route.get(NetwMultiplayerCore.SCENE_LIVE, 0))).is_equal(1)
	assert_int(
		int(on_session.get(NetwMultiplayerCore.INTEREST_COMMIT, 0))
	).is_equal(1)
	assert_int(int(on_session.get(NetwMultiplayerCore.SPAWNED, 0))).is_equal(0)
