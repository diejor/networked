#include "support/netw_test.h"

#include "netw/call_park.hpp"

namespace TestNetwCallPark {

using namespace godot;
using netw::NetwCallPark;

TEST_CASE(
    "[Networked][Rpc][Hosted] a parked call runs exactly once, whatever binds "
    "its route"
) {
    NetwCallPark park;
    const int64_t id = park.park(7, 31, 100);

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
        CHECK(park.park(7, 31, 100) > 0);
    }
    NETW_CHECK_EQ(park.active_count(7), NetwCallPark::BUDGET);

    NETW_CHECK_EQ(park.park(7, 31, 100), -1);
    NETW_CHECK_EQ(park.refused(), 1);
    NETW_CHECK_EQ(park.active_count(7), NetwCallPark::BUDGET);

    // The budget is per sender, so a quiet peer is not punished for a loud one.
    CHECK(park.park(9, 31, 100) > 0);
    NETW_CHECK_EQ(park.active_count(9), 1);
    NETW_CHECK_EQ(park.refused(), 1);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] a sweep drops what ran and what timed out, and "
    "gives the budget back"
) {
    NetwCallPark park;
    const int64_t ran = park.park(7, 31, 100);
    const int64_t waiting = park.park(7, 32, 100);
    const int64_t expiring = park.park(7, 33, 50);
    park.resolve(ran);

    park.sweep(60);

    NETW_CHECK_EQ(park.size(), 1);
    NETW_CHECK_EQ(park.active_count(7), 1);
    NETW_CHECK_EQ(park.route_of(waiting), 32);
    NETW_CHECK_EQ(park.route_of(ran), 0);
    NETW_CHECK_EQ(park.route_of(expiring), 0);

    // A row the sweep dropped can never run, which is what a timeout means.
    CHECK_FALSE(park.resolve(expiring));
    CHECK(park.resolve(waiting));
}

TEST_CASE(
    "[Networked][Rpc][Hosted] a cleared park owes nothing and has refused "
    "nothing"
) {
    NetwCallPark park;
    park.park(7, 31, 100);
    for (int index = 0; index < NetwCallPark::BUDGET; ++index) {
        park.park(9, 31, 100);
    }
    park.park(9, 31, 100);
    NETW_CHECK_EQ(park.refused(), 1);

    park.clear();

    NETW_CHECK_EQ(park.size(), 0);
    NETW_CHECK_EQ(park.refused(), 0);
    NETW_CHECK_EQ(park.active_count(9), 0);
}

} // namespace TestNetwCallPark
