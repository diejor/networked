#include "support/netw_test.h"

#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwLivenessAdoptionLaws {

using namespace netw_test;

/* A route is one identity everywhere, so whichever of its two doors arrives
 * second lands on the record the first one minted.
 *
 * The doors are a data row, which claims a route and mints a record with no
 * node, and a spawned entity, which mints a record and binds a route to it.
 * Both are flat verbs on the session, which is what makes these laws about the
 * route table rather than about the wrapper that reaches it.
 *
 * These cover the RECORD plane only. `entity_bind_route` takes a second path
 * when the entity already carries a wrapper, and a rig that stands entities up
 * route-before-node never reaches it. Covering that one needs a verb that
 * mints a wrapper with no record, which the rig does not have.
 */

godot::RID minted_by_row(LoopbackRig &p_rig, int64_t &r_route) {
    const godot::PackedInt64Array routes = p_rig.server()->call("claim_routes", 1);
    REQUIRE(routes.size() == 1);
    r_route = routes[0];
    return p_rig.server()->call("rid_from_route", r_route);
}

TEST_CASE("[Networked][Liveness] a row then a spawn converge on one record") {
    LoopbackRig rig(0);
    int64_t route = 0;
    const godot::RID minted = minted_by_row(rig, route);
    CHECK(minted.is_valid());

    const godot::RID entity
        = rig.declare_entity(EntityDecl().named("Late").on_route(int(route)));

    NETW_CHECK_EQ(int(rig.server()->call("entity_get_route", entity)), route);
    const godot::RID resolved = rig.server()->call("rid_from_route", route);
    CHECK(resolved == entity);
    const godot::PackedInt64Array live = rig.server()->call("live_routes");
    NETW_CHECK_EQ(live.size(), 1);
    NETW_CHECK_EQ(live[0], route);
}

TEST_CASE("[Networked][Liveness] a spawn then a row reuses the same record") {
    LoopbackRig rig(0);
    const godot::RID entity = rig.declare_entity(EntityDecl().named("Early"));
    const int64_t route
        = int64_t(rig.server()->call("entity_get_route", entity));
    CHECK(route > 0);

    godot::PackedInt64Array rows;
    rows.push_back(route);
    rig.server()->call("bind_routes_data", rows);

    CHECK(godot::RID(rig.server()->call("rid_from_route", route)) == entity);
    const godot::PackedInt64Array live = rig.server()->call("live_routes");
    NETW_CHECK_EQ(live.size(), 1);
    NETW_CHECK_EQ(live[0], route);
}

TEST_CASE("[Networked][Liveness] a parked callback flushes exactly once") {
    LoopbackRig rig(0);
    const int64_t route
        = int64_t(rig.server()->call("reserve_route")) + 1;
    const CallLog log;
    rig.server()->call("when_live", route, log.callable("live"));

    const godot::PackedInt64Array claimed
        = rig.server()->call("claim_routes", 1);
    NETW_CHECK_EQ(claimed[0], route);
    NETW_CHECK_EQ(log.count("live"), 1);

    rig.declare_entity(EntityDecl().named("Late").on_route(int(route)));
    NETW_CHECK_EQ(log.count("live"), 1);
}

TEST_CASE("[Networked][Liveness] a second wrapper is still a new record") {
    LoopbackRig rig(0);
    const godot::RID first = rig.declare_entity(EntityDecl().named("First"));
    const int64_t route
        = int64_t(rig.server()->call("entity_get_route", first));

    const godot::RID second
        = rig.declare_entity(EntityDecl().named("Second").on_route(int(route)));

    CHECK(second != first);
    CHECK(godot::RID(rig.server()->call("rid_from_route", route)) == second);
}

TEST_CASE("[Networked][Liveness] an adopted record answers with its node") {
    LoopbackRig rig(0);
    int64_t route = 0;
    const godot::RID minted = minted_by_row(rig, route);
    CHECK(godot::Object::cast_to<godot::Node>(
              rig.server()->call("entity_get_node", minted)
          )
          == nullptr);

    const godot::RID entity
        = rig.declare_entity(EntityDecl().named("Late").on_route(int(route)));
    godot::Node *owner = rig.node_of(entity);
    CHECK(owner != nullptr);
    CHECK(godot::Object::cast_to<godot::Node>(
              rig.server()->call("entity_get_node", entity)
          )
          == owner);
}

} // namespace TestNetwLivenessAdoptionLaws

#endif
