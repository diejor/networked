#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/entity.hpp"
#include "support/netw_call_log.h"

namespace TestRouteAdoptionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

Object *native_core(Object *p_api) {
    Object *core = p_api->get("_native_core");
    REQUIRE(core != nullptr);
    return core;
}

TEST_CASE(
    "[Networked][Liveness][Hosted] a wrapper the rig mints carries no record "
    "until a route adopts it"
) {
    LoopbackRig rig(0);
    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());

    Object *api = rig.server();
    Object *core = native_core(api);
    Object *liveness = core->get("liveness_core");
    REQUIRE(liveness != nullptr);

    const RID birth = wrapper->get_rid_handle();
    CHECK(birth.is_valid());
    NETW_CHECK_EQ(int(bool(liveness->call("entity_is_valid", birth))), 0);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] a route the data door minted and a wrapper "
    "that arrives after it converge on one record"
) {
    LoopbackRig rig(0);
    Object *api = rig.server();
    Object *core = native_core(api);

    const PackedInt64Array routes = api->call("claim_routes", 1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];
    const RID minted = api->call("entity_from_route", route);
    CHECK(minted.is_valid());

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    const RID birth = wrapper->get_rid_handle();

    NETW_CHECK_EQ(
        int(bool(core->call("liveness_bind_route", route, wrapper))),
        1
    );

    CHECK(wrapper->get_rid_handle() == minted);
    CHECK(birth != minted);
    CHECK(RID(api->call("entity_from_route", route)) == minted);
    NETW_CHECK_EQ(int64_t(api->call("entity_get_route", minted)), route);

    Object *owner = api->call("entity_get_node", minted);
    CHECK(owner != nullptr);
    CHECK(owner == wrapper->get_owner());
}

TEST_CASE(
    "[Networked][Liveness][Hosted] a second wrapper is refused a route another "
    "wrapper still holds"
) {
    LoopbackRig rig(0);
    Object *api = rig.server();
    Object *core = native_core(api);
    Object *liveness = core->get("liveness_core");
    REQUIRE(liveness != nullptr);

    const PackedInt64Array routes = api->call("claim_routes", 1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];

    const Ref<NetwEntity> first = rig.declare_unrecorded_wrapper("First");
    REQUIRE(first.is_valid());
    NETW_CHECK_EQ(
        int(bool(core->call("liveness_bind_route", route, first))),
        1
    );
    const RID held = first->get_rid_handle();

    const Ref<NetwEntity> second = rig.declare_unrecorded_wrapper("Second");
    REQUIRE(second.is_valid());
    NETW_CHECK_EQ(
        int(bool(core->call("liveness_bind_route", route, second))),
        0
    );

    CHECK(second->get_rid_handle() != held);
    NETW_CHECK_EQ(
        int(bool(liveness->call("entity_is_valid", second->get_rid_handle()))),
        0
    );
    CHECK(RID(api->call("entity_from_route", route)) == held);
    NETW_CHECK_EQ(int64_t(api->call("entity_get_epoch", held)), 0);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] a caller parked on a route is answered "
    "once when a wrapper is what makes the route live"
) {
    LoopbackRig rig(0);
    Object *api = rig.server();
    Object *core = native_core(api);

    const int64_t route = int64_t(core->call("liveness_reserve_route")) + 1;
    const CallLog parked;
    api->call("when_live", route, parked.callable("live"));

    const PackedInt64Array routes = api->call("claim_routes", 1);
    REQUIRE(routes.size() == 1);
    NETW_CHECK_EQ(routes[0], route);
    NETW_CHECK_EQ(parked.count("live"), 1);

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    NETW_CHECK_EQ(
        int(bool(core->call("liveness_bind_route", route, wrapper))),
        1
    );

    NETW_CHECK_EQ(parked.count("live"), 1);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] an adopted record answers by route with the "
    "wrapper that adopted it"
) {
    LoopbackRig rig(0);
    Object *api = rig.server();
    Object *core = native_core(api);

    const PackedInt64Array routes = api->call("claim_routes", 1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];
    CHECK(api->call("entity_get_node", api->call("entity_from_route", route))
          == Variant());

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    NETW_CHECK_EQ(
        int(bool(core->call("liveness_bind_route", route, wrapper))),
        1
    );

    CHECK(NetwEntity::by_route(route, api) == wrapper);

    const PackedInt64Array live = api->call("live_routes");
    REQUIRE(live.size() == 1);
    NETW_CHECK_EQ(live[0], route);
}

} // namespace TestRouteAdoptionLaws

#endif
