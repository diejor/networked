## Unit tests for [NetwLivenessCore], the record store behind
## [LivenessShell].
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
	assert_int(NetwLivenessCore.STATE_UNKNOWN) \
			.is_equal(LivenessShell.State.UNKNOWN)
	assert_int(NetwLivenessCore.STATE_LIVE) \
			.is_equal(NetwMultiplayer.EntityState.LIVE)
	assert_int(NetwLivenessCore.STATE_LIVE) \
			.is_equal(LivenessShell.State.LIVE)
	assert_int(NetwLivenessCore.STATE_LINGERING) \
			.is_equal(NetwMultiplayer.EntityState.LINGERING)
	assert_int(NetwLivenessCore.STATE_LINGERING) \
			.is_equal(LivenessShell.State.LINGERING)
	assert_int(NetwLivenessCore.STATE_DEAD) \
			.is_equal(NetwMultiplayer.EntityState.DEAD)
	assert_int(NetwLivenessCore.STATE_DEAD) \
			.is_equal(LivenessShell.State.DEAD)


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


## Verify a re-admission onto a tombstoned route binds a fresh handle and
## leaves the superseded record dead.
func test_revival_is_a_new_epoch() -> void:
	var first := core.entity_create()
	var route := core.reserve_route()
	core.bind_route(first, route)
	core.set_state(first, NetwLivenessCore.STATE_DEAD)

	var second := core.entity_create()
	assert_bool(core.bind_route(second, route)).is_true()

	assert_that(second).is_not_equal(first)
	assert_int(core.route_state(route)).is_equal(NetwLivenessCore.STATE_LIVE)
	assert_that(core.rid_from_route(route)).is_equal(second)
	assert_int(core.state_of(first)).is_equal(NetwLivenessCore.STATE_DEAD)


## Verify an unbound route reads UNKNOWN rather than DEAD, which is what keeps
## an early packet distinguishable from one naming a tombstone.
func test_unbound_route_is_unknown() -> void:
	assert_int(core.route_state(404)).is_equal(NetwLivenessCore.STATE_UNKNOWN)
	assert_int(core.route_of(RID())).is_equal(0)
