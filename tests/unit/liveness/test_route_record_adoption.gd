## Adoption laws for a route two doors reach: a table row and a spawned entity.
##
## A route is one identity everywhere, so whichever of the two arrives second
## must land on the record the first one minted. What is proven here is that
## both orderings converge on one record, that the convergence costs no second
## [method NetwMultiplayer.when_live] flush, and that a genuine re-admission
## still mints a new life.
class_name TestRouteRecordAdoption
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


## Verify a spawn arriving after a data bind adopts the record the row minted,
## which is the ordering that used to leave a live orphan behind.
func test_row_then_spawn_converges_on_one_record() -> void:
	var routes := api.claim_routes(1)
	var route := int(routes[0])
	var minted := api.rid_from_route(route)
	assert_bool(minted.is_valid()).is_true()

	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	assert_bool(entity.rid.is_valid()).is_false()

	api._liveness.bind_route(route, entity)

	assert_that(entity.rid).is_equal(minted)
	assert_that(api.rid_from_route(route)).is_equal(minted)
	assert_int(api.entity_get_route(minted)).is_equal(route)
	assert_array(api.live_routes()).is_equal(PackedInt64Array([route]))


## Verify the reverse ordering keeps reusing the record it already had, so the
## data bind never mints past a wrapper that got there first.
func test_spawn_then_row_reuses_the_same_record() -> void:
	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)

	var route := api.entity_allocate_route(api.rid_of(owner_node))
	var held := entity.rid

	api._liveness.bind_routes_data(PackedInt64Array([route]))

	assert_that(entity.rid).is_equal(held)
	assert_that(api.rid_from_route(route)).is_equal(held)
	assert_array(api.live_routes()).is_equal(PackedInt64Array([route]))


## Verify the adoption costs no second flush, so a caller parked on the route
## is answered once however the two doors are ordered.
func test_a_parked_callback_flushes_exactly_once() -> void:
	var route := api.reserve_route() + 1
	var fired := [0]
	api.when_live(route, func() -> void: fired[0] += 1)

	assert_int(int(api.claim_routes(1)[0])).is_equal(route)
	assert_int(fired[0]).is_equal(1)

	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	api._liveness.bind_route(route, entity)

	assert_int(fired[0]).is_equal(1)


## Verify a wrapper arriving for a route another wrapper already holds is still
## a re-admission, so adoption never lets two entities share one record.
func test_a_second_wrapper_is_still_a_new_record() -> void:
	var first_node := Node2D.new()
	var first := NetwEntity.ensure(first_node)
	add_child(first_node)
	auto_free(first_node)
	var route := api.entity_allocate_route(api.rid_of(first_node))
	var first_rid := first.rid

	var second_node := Node2D.new()
	var second := NetwEntity.ensure(second_node)
	add_child(second_node)
	auto_free(second_node)
	api._liveness.bind_route(route, second)

	assert_that(second.rid).is_not_equal(first_rid)
	assert_that(api.rid_from_route(route)).is_equal(second.rid)


## Verify an adopted record still answers to the wrapper's own node, since the
## record a row minted carries no node of its own until one adopts it.
func test_an_adopted_record_answers_with_the_wrapper_node() -> void:
	var route := int(api.claim_routes(1)[0])
	assert_object(api.entity_get_node(api.rid_from_route(route))).is_null()

	var owner_node := Node2D.new()
	var entity := NetwEntity.ensure(owner_node)
	add_child(owner_node)
	auto_free(owner_node)
	api._liveness.bind_route(route, entity)

	assert_object(api.entity_get_node(entity.rid)).is_same(owner_node)
	assert_that(NetwEntity.by_route(route, api)).is_equal(entity)
