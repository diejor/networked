#include "support/netw_test.h"

#include "netw/liveness_core.hpp"
#include "netw/spawn/park.hpp"

namespace TestNetwPark {

using namespace godot;
using netw::spawn::Park;

PackedByteArray frame(uint8_t marker) {
    PackedByteArray out;
    out.push_back(marker);
    return out;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a parked frame is taken once, and a route that "
    "parked nothing answers nothing"
) {
    Park park;

    CHECK(park.park(31, frame(7), Park::WAIT_ROUTE, 0));
    CHECK(park.has(31));
    NETW_CHECK_EQ(park.size(), 1);

    CHECK(park.take(31) == frame(7));
    CHECK_FALSE(park.has(31));
    NETW_CHECK_EQ(park.take(31).size(), 0);
    NETW_CHECK_EQ(park.take(99).size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the same spawn arriving twice keeps the frame "
    "it parked with"
) {
    Park park;
    park.park(31, frame(7), Park::WAIT_ROUTE, 0);

    CHECK_FALSE(park.park(31, frame(9), Park::WAIT_SCENE, 500));

    NETW_CHECK_EQ(park.size(), 1);
    CHECK(park.take(31) == frame(7));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a cancelled park applies nothing, which is a "
    "despawn arriving mid-park"
) {
    Park park;
    park.park(31, frame(7), Park::WAIT_ROUTE, 0);

    CHECK(park.cancel(31));
    CHECK_FALSE(park.cancel(31));
    NETW_CHECK_EQ(park.take(31).size(), 0);
    NETW_CHECK_EQ(park.size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] what a row waits on decides who retries it"
) {
    Park park;
    park.park(31, frame(1), Park::WAIT_ROUTE, 0);
    park.park(32, frame(2), Park::WAIT_SCENE, 500);
    park.park(33, frame(3), Park::WAIT_SCENE, 900);

    PackedInt64Array on_route;
    on_route.push_back(31);
    CHECK(park.waiting_on(Park::WAIT_ROUTE) == on_route);
    NETW_CHECK_EQ(park.waiting_on(Park::WAIT_SCENE).size(), 2);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a wait carrying a deadline expires on the "
    "wall clock, and a route wait carries none because its waiter bounds it"
) {
    Park park;
    park.park(31, frame(1), Park::WAIT_ROUTE, 0);
    park.park(32, frame(2), Park::WAIT_SCENE, 500);
    park.park(33, frame(3), Park::WAIT_ADOPT, 700);

    CHECK_FALSE(park.is_expired(32, 499));
    CHECK(park.is_expired(32, 500));
    CHECK(park.is_expired(32, 5000));

    CHECK_FALSE(park.is_expired(33, 699));
    CHECK(park.is_expired(33, 700));

    CHECK_FALSE(park.is_expired(31, 5000));
    CHECK_FALSE(park.is_expired(99, 5000));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a peeked payload stays parked, so a retry "
    "that does not land keeps the deadline it was parked under"
) {
    Park park;
    park.park(31, frame(7), Park::WAIT_ADOPT, 500);

    CHECK(park.peek(31) == frame(7));
    CHECK(park.has(31));
    CHECK_FALSE(park.park(31, frame(9), Park::WAIT_ADOPT, 9000));
    CHECK(park.is_expired(31, 500));

    CHECK(park.peek(404).is_empty());
}

TEST_CASE("[Networked][Spawn][Hosted] a cleared park is waiting for nothing") {
    Park park;
    park.park(31, frame(1), Park::WAIT_ROUTE, 0);
    park.park(32, frame(2), Park::WAIT_SCENE, 500);

    park.clear();

    NETW_CHECK_EQ(park.size(), 0);
    CHECK_FALSE(park.has(31));
    NETW_CHECK_EQ(park.waiting_on(Park::WAIT_SCENE).size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] an anchor parks only where its route "
    "could still arrive, so a tombstone is not a waiting room"
) {
    CHECK(Park::anchor_parks(netw::NetwLivenessCore::STATE_UNKNOWN));
    CHECK(Park::anchor_parks(netw::NetwLivenessCore::STATE_ABSENT));
    CHECK_FALSE(Park::anchor_parks(netw::NetwLivenessCore::STATE_DEAD));
    CHECK_FALSE(Park::anchor_parks(netw::NetwLivenessCore::STATE_LIVE));
    CHECK_FALSE(Park::anchor_parks(netw::NetwLivenessCore::STATE_LINGERING));
}

} // namespace TestNetwPark
