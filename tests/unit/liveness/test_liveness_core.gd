## Unit tests for [NetwLivenessCore], the record store behind
## [NetwMultiplayerCore].
class_name TestLivenessCore
extends NetwTestSuite

var core: NetwLivenessCore


## Set up a fresh record store, unattached to any session.
func before_test() -> void:
	core = NetwLivenessCore.new()


## Verify the public state enum agrees with the native core value by value.
func test_state_enums_agree() -> void:
	assert_int(NetwLivenessCore.STATE_UNKNOWN) \
			.is_equal(NetwMultiplayer.EntityState.UNKNOWN)
	assert_int(NetwLivenessCore.STATE_LIVE) \
			.is_equal(NetwMultiplayer.EntityState.LIVE)
	assert_int(NetwLivenessCore.STATE_LINGERING) \
			.is_equal(NetwMultiplayer.EntityState.LINGERING)
	assert_int(NetwLivenessCore.STATE_DEAD) \
			.is_equal(NetwMultiplayer.EntityState.DEAD)


## Verify a bound route reaches LIVE and resolves back to its own handle.
func test_bind_route_binds_both_directions() -> void:
	var entity := core.entity_create()
	var route := core.reserve_route()

	assert_bool(core.bind_route(entity, route)).is_true()
	assert_int(core.route_state(route)).is_equal(NetwLivenessCore.STATE_LIVE)
	assert_int(core.route_of(entity)).is_equal(route)
	assert_that(core.rid_from_route(route)).is_equal(entity)


## Verify a tombstoned route stays dead and refuses to move backward.
func test_tombstone_is_permanent() -> void:
	var entity := core.entity_create()
	var route := core.reserve_route()
	core.bind_route(entity, route)

	assert_bool(core.set_state(entity, NetwLivenessCore.STATE_DEAD)).is_true()
	assert_int(core.route_state(route)).is_equal(NetwLivenessCore.STATE_DEAD)
	assert_bool(core.set_state(entity, NetwLivenessCore.STATE_LIVE)).is_false()
	assert_int(core.route_state(route)).is_equal(NetwLivenessCore.STATE_DEAD)


## Verify a re-admission onto a tombstoned route revives the record that wore
## the tombstone, one epoch higher, and refuses any other handle asking for it.
func test_revival_is_a_new_epoch() -> void:
	var entity := core.entity_create()
	var route := core.reserve_route()
	core.bind_route(entity, route)
	core.set_state(entity, NetwLivenessCore.STATE_DEAD)
	assert_int(core.epoch_of(entity)).is_equal(0)

	var stranger := core.entity_create()
	assert_bool(core.bind_route(stranger, route)).is_false()
	assert_int(core.epoch_of(stranger)).is_equal(0)

	assert_bool(core.bind_route(entity, route)).is_true()

	assert_int(core.epoch_of(entity)).is_equal(1)
	assert_int(core.route_state(route)).is_equal(NetwLivenessCore.STATE_LIVE)
	assert_that(core.rid_from_route(route)).is_equal(entity)


## Verify an unbound route reads UNKNOWN rather than DEAD, which is what keeps
## an early packet distinguishable from one naming a tombstone.
func test_unbound_route_is_unknown() -> void:
	assert_int(core.route_state(404)).is_equal(NetwLivenessCore.STATE_UNKNOWN)
	assert_int(core.route_of(RID())).is_equal(0)
