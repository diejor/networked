## Identity laws for the session-band route verbs a table row is minted
## through.
##
## A row's identity is a route and nothing else. What is proven here is that a
## claimed route is as live as a node entity's, that it costs no per-row signal,
## and that releasing it tombstones rather than freeing.
class_name TestTableIdentity
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


## Verify a claimed route is live, monotonic, and resolvable back to a handle,
## so a row is an entity the moment anyone needs it to be.
func test_claimed_routes_are_live_and_monotonic() -> void:
	var routes := api.claim_routes(3)

	assert_int(routes.size()).is_equal(3)
	assert_int(routes[0]).is_greater(0)
	assert_int(routes[1]).is_equal(routes[0] + 1)
	assert_int(routes[2]).is_equal(routes[1] + 1)
	for value in routes:
		assert_int(api.route_get_state(int(value))).is_equal(
			NetwMultiplayer.EntityState.LIVE,
		)
		assert_bool(api.rid_from_route(int(value)).is_valid()).is_true()


## Verify a claimed row carries no node, which is the whole point: there is no
## other route to a replicated thing that is not in the tree.
func test_a_claimed_row_carries_no_node() -> void:
	var routes := api.claim_routes(1)
	var entity := api.rid_from_route(int(routes[0]))

	assert_object(api.entity_get_node(entity)).is_null()
	assert_int(api.entity_get_route(entity)).is_equal(int(routes[0]))
	assert_int(api.entity_get_state(entity)).is_equal(
		NetwMultiplayer.EntityState.LIVE,
	)


## Verify a wave of rows costs no per-row signal, because two thousand
## dispatches is exactly the per-row crossing cohorts exist to avoid.
func test_a_wave_of_rows_emits_no_per_row_signal() -> void:
	var seen: Array[int] = []
	api._liveness.entity_live.connect(
		func(route: int, _entity: NetwEntity) -> void:
			seen.append(route),
	)

	api.claim_routes(50)

	assert_array(seen).is_empty()


## Verify a callback parked on one particular route still runs, so a caller who
## asked about a row by name is answered even though the wave is silent.
func test_a_parked_when_live_callback_still_runs() -> void:
	var next_route := api.reserve_route() + 1
	var fired: Array[int] = []
	api.when_live(next_route, func() -> void: fired.append(next_route))
	assert_array(fired).is_empty()

	api.claim_routes(1)

	assert_array(fired).is_equal([next_route])


## Verify releasing tombstones the routes and queues them for the reliable
## lifecycle stream, so every peer retires the same identities.
func test_releasing_tombstones_and_queues_the_lifecycle_removal() -> void:
	var routes := api.claim_routes(2)

	assert_int(api.release_routes(routes)).is_equal(OK)

	for value in routes:
		assert_int(api.route_get_state(int(value))).is_equal(
			NetwMultiplayer.EntityState.DEAD,
		)
	assert_array(api._table_core.lifecycle_removals()).is_equal(routes)


## Verify a tombstoned route is retired for the session, so an upsert that
## arrives late cannot resurrect the row it named.
func test_a_tombstoned_route_is_never_reissued() -> void:
	var routes := api.claim_routes(1)
	api.release_routes(routes)

	var fresh := api.claim_routes(1)

	assert_int(fresh[0]).is_greater(int(routes[0]))
	assert_int(api.route_get_state(int(routes[0]))).is_equal(
		NetwMultiplayer.EntityState.DEAD,
	)


## Verify the degenerate calls answer rather than allocate.
func test_the_degenerate_calls_answer_without_allocating() -> void:
	assert_array(api.claim_routes(0)).is_empty()
	assert_array(api.claim_routes(-4)).is_empty()
	assert_int(api.release_routes(PackedInt64Array())).is_equal(OK)
	assert_array(api._table_core.lifecycle_removals()).is_empty()


## Verify session teardown drops the pending lifecycle removals, since a new
## session's peers never issued those routes.
func test_session_teardown_drops_the_pending_removals() -> void:
	api.release_routes(api.claim_routes(2))
	assert_int(api._table_core.lifecycle_removals().size()).is_equal(2)

	api._clear_flat_family_state()

	assert_array(api._table_core.lifecycle_removals()).is_empty()
