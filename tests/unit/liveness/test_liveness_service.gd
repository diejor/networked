## Unit tests for [LivenessService] and [NetwLiveness] facade.
class_name TestLivenessService
extends NetwTestSuite

const LivenessService := preload("res://addons/networked/session/liveness/liveness_service.gd")

var mt: MultiplayerTree
var service: LivenessService


## Set up a fresh MultiplayerTree and LivenessService.
func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	service = mt.get_service(LivenessService) as LivenessService


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

	var route1 := service.allocate_route(entity1)
	var route2 := service.allocate_route(entity2)

	assert_that(route1).is_equal(1)
	assert_that(route2).is_equal(2)
	assert_that(service.entity_of(route1)).is_equal(entity1)
	assert_that(service.entity_of(route2)).is_equal(entity2)


## Verify that binding transitions the state to LIVE.
func test_bind_transitions_to_live() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	var route := 42

	service.bind_route(route, entity)

	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)
	assert_that(service.state_of(entity)).is_equal(LivenessService.State.LIVE)
	assert_that(service.route_of(entity)).is_equal(route)


## Verify that despawning with linger transitions state: LIVE -> LINGERING -> DEAD.
func test_despawn_linger_transitions() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	var me := MultiplayerEntity.new()
	owner_node.add_child(me)
	me.owner = owner_node # Explicit owner set before tree entry
	entity.multiplayer_entity = me

	add_child(owner_node) # Tree entry triggers _on_owner_tree_entered
	auto_free(owner_node)

	var route := service.allocate_route(entity)
	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)

	var opts := MultiplayerEntity.DespawnOpts.new()
	opts.linger = true
	opts.linger_seconds = 0.05

	me.despawn(opts)
	assert_that(service.route_state(route)).is_equal(
		LivenessService.State.LINGERING
	)

	# Wait for linger timer to fire and node to exit tree
	await owner_node.tree_exited
	assert_that(service.route_state(route)).is_equal(LivenessService.State.DEAD)


## Verify that plain despawning transitions state: LIVE -> DEAD immediately.
func test_plain_despawn_transitions() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	var me := MultiplayerEntity.new()
	owner_node.add_child(me)
	me.owner = owner_node # Explicit owner set before tree entry
	entity.multiplayer_entity = me

	add_child(owner_node)
	auto_free(owner_node)

	var route := service.allocate_route(entity)
	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)

	me.despawn()
	await owner_node.tree_exited
	assert_that(service.route_state(route)).is_equal(LivenessService.State.DEAD)


## Verify that reparenting does not transition the state to DEAD.
func test_reparent_does_not_kill_route() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)

	var route := service.allocate_route(entity)
	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)

	# Simulate reparenting in flight
	entity.reparenting = MultiplayerEntity.ReparentOpts.new()

	owner_node.get_parent().remove_child(owner_node)
	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)

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
	service.bind_route(route1, entity)
	service.when_live(route1, func(): fired1[0] = true)
	assert_that(fired1[0]).override_failure_message("fired1 should be true").is_true()

	# 2. Fires on late bind
	service.when_live(route2, func(): fired2[0] = true)
	assert_that(fired2[0]).override_failure_message("fired2 should be false before bind").is_false()
	service.bind_route(route2, entity)
	assert_that(fired2[0]).override_failure_message("fired2 should be true after bind").is_true()

	# 3. Expires on timeout (use small timeout)
	service.when_live(route3, func(): fired3[0] = true, 2)
	assert_that(fired3[0]).is_false()
	
	# Advance frames to trigger timeout
	for i in 5:
		await get_tree().process_frame

	assert_that(fired3[0]).is_false()
	assert_that(service._pending_live.has(route3)).is_false()


## Verify that session ending clears all liveness state.
func test_session_ended_leaves_no_residue() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	var route := service.allocate_route(entity)
	assert_that(service.route_state(route)).is_equal(LivenessService.State.LIVE)

	mt.session_ended.emit()
	await get_tree().process_frame # deferred clear

	assert_that(service.route_state(route)).is_equal(
		LivenessService.State.UNKNOWN
	)
	assert_that(service._route_counter).is_equal(0)
