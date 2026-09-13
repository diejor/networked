#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "support/netw_call_log.h"

namespace TestRouteAdoptionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

TEST_CASE(
    "[Networked][Liveness] a wrapper the rig mints carries no record "
    "until a route adopts it"
) {
    LoopbackRig rig(0);
    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());

    netw::NetwMultiplayer *session = rig.server();
    REQUIRE(session != nullptr);

    const RID birth = wrapper->get_rid_handle();
    CHECK(birth.is_valid());
    NETW_CHECK_EQ(int(session->get_liveness_core()->entity_is_valid(birth)), 0);
}

TEST_CASE(
    "[Networked][Liveness] a route the data door minted and a wrapper "
    "that arrives after it converge on one record"
) {
    LoopbackRig rig(0);
    netw::NetwMultiplayer *api = rig.server();

    const PackedInt64Array routes = api->liveness_claim_routes(1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];
    const RID minted = api->entity_from_route(route);
    CHECK(minted.is_valid());

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    const RID birth = wrapper->get_rid_handle();

    NETW_CHECK_EQ(int(api->liveness_bind_route(route, wrapper.ptr())), 1);

    CHECK(wrapper->get_rid_handle() == minted);
    CHECK(birth != minted);
    CHECK(api->entity_from_route(route) == minted);
    NETW_CHECK_EQ(api->entity_get_route(minted), route);

    Node *owner = api->entity_get_node(minted);
    CHECK(owner != nullptr);
    CHECK(owner == wrapper->get_owner());
}

TEST_CASE(
    "[Networked][Liveness] a second wrapper is refused a route another "
    "wrapper still holds"
) {
    LoopbackRig rig(0);
    netw::NetwMultiplayer *api = rig.server();

    const PackedInt64Array routes = api->liveness_claim_routes(1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];

    const Ref<NetwEntity> first = rig.declare_unrecorded_wrapper("First");
    REQUIRE(first.is_valid());
    NETW_CHECK_EQ(int(api->liveness_bind_route(route, first.ptr())), 1);
    const RID held = first->get_rid_handle();

    const Ref<NetwEntity> second = rig.declare_unrecorded_wrapper("Second");
    REQUIRE(second.is_valid());
    NETW_CHECK_EQ(int(api->liveness_bind_route(route, second.ptr())), 0);

    CHECK(second->get_rid_handle() != held);
    NETW_CHECK_EQ(
        int(
            api->get_liveness_core()->entity_is_valid(second->get_rid_handle())
        ),
        0
    );
    CHECK(api->entity_from_route(route) == held);
    NETW_CHECK_EQ(api->entity_get_epoch(held), 0);
}

TEST_CASE(
    "[Networked][Liveness] a caller parked on a route is answered "
    "once when a wrapper is what makes the route live"
) {
    LoopbackRig rig(0);
    netw::NetwMultiplayer *api = rig.server();

    const int64_t route = api->liveness_reserve_route() + 1;
    const CallLog parked;
    api->liveness_when_live(route, parked.callable("live"), 0, Callable());

    const PackedInt64Array routes = api->liveness_claim_routes(1);
    REQUIRE(routes.size() == 1);
    NETW_CHECK_EQ(routes[0], route);
    NETW_CHECK_EQ(parked.count("live"), 1);

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    NETW_CHECK_EQ(int(api->liveness_bind_route(route, wrapper.ptr())), 1);

    NETW_CHECK_EQ(parked.count("live"), 1);
}

TEST_CASE(
    "[Networked][Liveness] an adopted record answers by route with the "
    "wrapper that adopted it"
) {
    LoopbackRig rig(0);
    netw::NetwMultiplayer *api = rig.server();

    const PackedInt64Array routes = api->liveness_claim_routes(1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];
    CHECK(api->entity_get_node(api->entity_from_route(route)) == nullptr);

    const Ref<NetwEntity> wrapper = rig.declare_unrecorded_wrapper("Adopted");
    REQUIRE(wrapper.is_valid());
    NETW_CHECK_EQ(int(api->liveness_bind_route(route, wrapper.ptr())), 1);

    CHECK(NetwEntity::by_route(route, api) == wrapper);

    const PackedInt32Array live = api->liveness_get_routes();
    REQUIRE(live.size() == 1);
    NETW_CHECK_EQ(live[0], route);
}

} // namespace TestRouteAdoptionLaws

#endif
