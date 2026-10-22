#include "support/netw_test.h"

#include "netw/call_park.hpp"
#include "netw/liveness_core.hpp"

namespace TestNetwCallPark {

using namespace godot;
using netw::NetwCallPark;
using netw::NetwLivenessCore;

TEST_CASE(
    "[Networked][Rpc][Hosted] a parked call runs exactly once, whatever binds "
    "its route"
) {
    NetwCallPark park;
    const int64_t id = park.park(7, 31, 0, 100);

    CHECK(id > 0);
    NETW_CHECK_EQ(park.route_of(id), 31);
    CHECK(park.resolve(id));
    CHECK_FALSE(park.resolve(id));
    CHECK_FALSE(park.resolve(id + 1000));
}

TEST_CASE(
    "[Networked][Rpc][Hosted] a sender is held to its budget, and the refusals "
    "are counted rather than queued"
) {
    NetwCallPark park;

    for (int index = 0; index < NetwCallPark::BUDGET; ++index) {
        CHECK(park.park(7, 31, 0, 100) > 0);
    }
    NETW_CHECK_EQ(park.active_count(7), NetwCallPark::BUDGET);

    NETW_CHECK_EQ(park.park(7, 31, 0, 100), -1);
    NETW_CHECK_EQ(park.refused(), 1);
    NETW_CHECK_EQ(park.active_count(7), NetwCallPark::BUDGET);

    CHECK(park.park(9, 31, 0, 100) > 0);
    NETW_CHECK_EQ(park.active_count(9), 1);
    NETW_CHECK_EQ(park.refused(), 1);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] a sweep drops what ran and what timed out, and "
    "gives the budget back"
) {
    Ref<NetwLivenessCore> liveness;
    liveness.instantiate();
    NetwCallPark park;
    const int64_t ran = park.park(7, 31, 0, 100);
    const int64_t waiting = park.park(7, 32, 0, 100);
    const int64_t expiring = park.park(7, 33, 0, 50);
    park.resolve(ran);

    park.sweep(60, *liveness.ptr());

    NETW_CHECK_EQ(park.size(), 1);
    NETW_CHECK_EQ(park.active_count(7), 1);
    NETW_CHECK_EQ(park.route_of(waiting), 32);
    NETW_CHECK_EQ(park.route_of(ran), 0);
    NETW_CHECK_EQ(park.route_of(expiring), 0);

    CHECK_FALSE(park.resolve(expiring));
    CHECK(park.resolve(waiting));
}

TEST_CASE(
    "[Networked][Rpc][Hosted] LH1 LH5 a call to a route whose spawn is held "
    "waits as long as the hold, and its deadline counts from the release"
) {
    Ref<NetwLivenessCore> liveness;
    liveness.instantiate();
    NetwCallPark park;
    liveness->hold(31);
    const int64_t dispatched = park.park(7, 31, 0, 30);
    const int64_t released = park.park(7, 31, 0, 30);

    for (int64_t tick = 1; tick <= 300; ++tick) {
        park.sweep(tick, *liveness.ptr());
    }
    CHECK(park.resolve(dispatched));

    liveness->release(31);
    park.sweep(329, *liveness.ptr());
    NETW_CHECK_EQ(park.route_of(released), 31);
    park.sweep(330, *liveness.ptr());
    NETW_CHECK_EQ(park.route_of(released), 0);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] a cleared park owes nothing and has refused "
    "nothing"
) {
    NetwCallPark park;
    park.park(7, 31, 0, 100);
    for (int index = 0; index < NetwCallPark::BUDGET; ++index) {
        park.park(9, 31, 0, 100);
    }
    park.park(9, 31, 0, 100);
    NETW_CHECK_EQ(park.refused(), 1);

    park.clear();

    NETW_CHECK_EQ(park.size(), 0);
    NETW_CHECK_EQ(park.refused(), 0);
    NETW_CHECK_EQ(park.active_count(9), 0);
}

} // namespace TestNetwCallPark
