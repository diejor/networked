## Unit tests for the flat [NetwMultiplayer] liveness surface.
class_name TestLivenessService
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


## Set up a fresh MultiplayerTree and its api.
func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


## Verify the flat entity-state enum matches the native record store.
func test_entity_state_matches_the_native_core() -> void:
	assert_int(NetwMultiplayer.EntityState.UNKNOWN).is_equal(
		NetwLivenessCore.STATE_UNKNOWN,
	)
	assert_int(NetwMultiplayer.EntityState.LIVE).is_equal(
		NetwLivenessCore.STATE_LIVE,
	)
	assert_int(NetwMultiplayer.EntityState.LINGERING).is_equal(
		NetwLivenessCore.STATE_LINGERING,
	)
	assert_int(NetwMultiplayer.EntityState.DEAD).is_equal(
		NetwLivenessCore.STATE_DEAD,
	)


## Verify a data-only entity uses the route bridge without a wrapper node.
func test_data_entity_has_no_node_and_uses_the_same_route_bridge() -> void:
	var entity := api.entity_create()

	assert_bool(entity.is_valid()).is_true()
	assert_object(api.entity_get_node(entity)).is_null()
	assert_int(api.entity_get_state(entity)).is_equal(
		NetwMultiplayer.EntityState.UNKNOWN,
	)
	var route := api.entity_allocate_route(entity)
	assert_int(route).is_equal(1)
	assert_that(api.rid_from_route(route)).is_equal(entity)
	assert_int(api.entity_get_state(entity)).is_equal(
		NetwMultiplayer.EntityState.LIVE,
	)


## Verify that route allocation is monotonic and starts at 1.
func test_allocation_is_monotonic() -> void:
	var owner_node := Node2D.new()
	var entity1 := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)

	var owner_node2 := Node2D.new()
	var entity2 := NetwEntity.ensure(owner_node2)
	add_child(owner_node2)
	auto_free(owner_node2)

	var rid1 := api.rid_of(entity1.owner)
	var rid2 := api.rid_of(entity2.owner)
	var route1 := api.entity_allocate_route(rid1)
	var route2 := api.entity_allocate_route(rid2)

	assert_that(route1).is_equal(1)
	assert_that(route2).is_equal(2)
	assert_that(NetwEntity.by_route(route1, api)).is_equal(entity1)
	assert_that(NetwEntity.by_route(route2, api)).is_equal(entity2)


## Verify that binding transitions the state to LIVE.
func test_bind_transitions_to_live() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	var route := 42
	var emitted: Array = []
	api.entity_live.connect(
		func(live_route: int, live_entity: NetwEntity) -> void:
			emitted.append([live_route, live_entity]),
	)

	var rid := api.rid_of(entity.owner)
	assert_int(api.entity_bind_route(rid, route)).is_equal(OK)

	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)
	assert_int(api.entity_get_state(rid)).is_equal(NetwMultiplayer.EntityState.LIVE)
	assert_int(api.entity_get_route(rid)).is_equal(route)
	assert_array(emitted).contains([[route, entity]])


## Verify that a re-admission onto a tombstoned route is a new life.
##
## The route is the wire name and outlives the record, so a spawn frame arriving
## after the despawn that tombstoned it rebinds the same route. The record it
## rebinds onto is a new one: the entity gives up its [member NetwEntity.rid] at
## death, so a packet still carrying the old handle can never resolve onto the
## life that replaced it.
func test_revival_binds_a_new_record() -> void:
	var first_node := Node2D.new()
	var first_entity := NetwEntity.ensure(first_node)
	add_child(first_node)
	auto_free(first_node)

	var route := api.entity_allocate_route(api.rid_of(first_node))
	var first_rid := first_entity.rid
	assert_bool(first_rid.is_valid()).is_true()

	first_entity.despawn()
	await first_node.tree_exited
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.DEAD)
	assert_bool(first_entity.rid.is_valid()).is_false()
	assert_that(NetwEntity.by_route(route, api)).is_null()
	assert_int(api.entity_get_route(first_rid)).is_equal(route)
	assert_int(api.entity_get_state(first_rid)).is_equal(
		NetwMultiplayer.EntityState.DEAD,
	)

	var second_node := Node2D.new()
	var second_entity := NetwEntity.ensure(second_node)
	add_child(second_node)
	auto_free(second_node)
	var second_rid := api.rid_of(second_node)
	assert_int(api.entity_bind_route(second_rid, route)).is_equal(OK)

	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)
	assert_that(NetwEntity.by_route(route, api)).is_equal(second_entity)
	assert_that(second_entity.rid).is_not_equal(first_rid)


## Verify that linger transitions state from LIVE to LINGERING to DEAD.
func test_despawn_linger_transitions() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)

	add_child(owner_node) # Tree entry triggers _handle_tree_entered
	auto_free(owner_node)

	var route := api.entity_allocate_route(api.rid_of(entity.owner))
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)
	var transitions: Array[StringName] = []
	api.entity_lingering.connect(
		func(_route: int, _entity: NetwEntity) -> void:
			transitions.append(&"lingering"),
	)
	api.entity_dead.connect(
		func(_route: int) -> void:
			transitions.append(&"dead"),
	)

	var opts := NetwEntity.DespawnOpts.new()
	opts.linger = true
	opts.linger_seconds = 0.05

	entity.despawn(opts)
	assert_int(api.route_get_state(route)).is_equal(
		NetwMultiplayer.EntityState.LINGERING,
	)

	# Wait for linger timer to fire and node to exit tree
	await owner_node.tree_exited
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.DEAD)
	assert_array(transitions).is_equal([&"lingering", &"dead"])


## Verify that plain despawning transitions state: LIVE -> DEAD immediately.
func test_plain_despawn_transitions() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)

	add_child(owner_node)
	auto_free(owner_node)

	var route := api.entity_allocate_route(api.rid_of(entity.owner))
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)

	entity.despawn()
	await owner_node.tree_exited
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.DEAD)


## Verify that reparenting does not transition the state to DEAD.
func test_reparent_does_not_kill_route() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)

	var route := api.entity_allocate_route(api.rid_of(entity.owner))
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)

	# Simulate reparenting in flight
	entity.reparenting = NetwEntity.ReparentOpts.new()

	owner_node.get_parent().remove_child(owner_node)
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)

	# Cleanup
	owner_node.free()


## Verify when_live behavior: fires immediately, on late bind, and times out.
func test_when_live_fires() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)

	var route1 := 101
	var route2 := 102
	var route3 := 103

	var fired1 := [false]
	var fired2 := [false]
	var fired3 := [false]

	# 1. Fires immediately if LIVE
	var rid := api.rid_of(entity.owner)
	api.entity_bind_route(rid, route1)
	api.when_live(route1, func(): fired1[0] = true)
	assert_that(fired1[0]) \
			.override_failure_message("fired1 should be true") \
			.is_true()

	# 2. Fires on late bind
	api.when_live(route2, func(): fired2[0] = true)
	assert_that(fired2[0]) \
			.override_failure_message("fired2 should be false before bind") \
			.is_false()
	api.entity_bind_route(rid, route2)
	assert_that(fired2[0]) \
			.override_failure_message("fired2 should be true after bind") \
			.is_true()

	# 3. Expires on timeout (use small timeout)
	api.when_live(route3, func(): fired3[0] = true, 2)
	assert_that(fired3[0]).is_false()

	# Advance frames to trigger timeout
	for i in 5:
		await get_tree().process_frame

	assert_that(fired3[0]).is_false()
	assert_int(
		api.get_stat(NetwMultiplayer.Stat.STAT_PENDING_LIVE),
	).is_equal(0)


## Verify that session ending clears all liveness state.
func test_session_ended_leaves_no_residue() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	var route := api.entity_allocate_route(api.rid_of(entity.owner))
	assert_int(api.route_get_state(route)).is_equal(NetwMultiplayer.EntityState.LIVE)

	mt.api.session_ended.emit()
	await get_tree().process_frame # deferred clear

	assert_int(api.route_get_state(route)).is_equal(
		NetwMultiplayer.EntityState.UNKNOWN,
	)
	assert_int(api.live_routes().size()).is_equal(0)
	assert_int(api.reserve_route()).is_equal(1)
	assert_bool(entity.rid.is_valid()).is_false()
