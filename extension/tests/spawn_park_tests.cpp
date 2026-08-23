#include "support/netw_test.h"

#include "netw/api/liveness_core.hpp"
#include "netw/spawn_park.hpp"

namespace TestNetwSpawnPark {

using namespace godot;
using netw::NetwSpawnPark;

Ref<NetwSpawnPark> fresh() {
    Ref<NetwSpawnPark> park;
    park.instantiate();
    return park;
}

PackedByteArray frame(uint8_t marker) {
    PackedByteArray out;
    out.push_back(marker);
    return out;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a parked frame is taken once, and a route that "
    "parked nothing answers nothing"
) {
    const Ref<NetwSpawnPark> park = fresh();

    CHECK(park->park(31, frame(7), NetwSpawnPark::WAIT_ROUTE, 0));
    CHECK(park->has(31));
    NETW_CHECK_EQ(park->size(), 1);

    CHECK(park->take(31) == frame(7));
    CHECK_FALSE(park->has(31));
    NETW_CHECK_EQ(park->take(31).size(), 0);
    NETW_CHECK_EQ(park->take(99).size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the same spawn arriving twice keeps the frame "
    "it parked with"
) {
    const Ref<NetwSpawnPark> park = fresh();
    park->park(31, frame(7), NetwSpawnPark::WAIT_ROUTE, 0);

    CHECK_FALSE(park->park(31, frame(9), NetwSpawnPark::WAIT_SCENE, 500));

    NETW_CHECK_EQ(park->size(), 1);
    CHECK(park->take(31) == frame(7));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a cancelled park applies nothing, which is a "
    "despawn arriving mid-park"
) {
    const Ref<NetwSpawnPark> park = fresh();
    park->park(31, frame(7), NetwSpawnPark::WAIT_ROUTE, 0);

    CHECK(park->cancel(31));
    CHECK_FALSE(park->cancel(31));
    NETW_CHECK_EQ(park->take(31).size(), 0);
    NETW_CHECK_EQ(park->size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] what a row waits on decides who retries it"
) {
    const Ref<NetwSpawnPark> park = fresh();
    park->park(31, frame(1), NetwSpawnPark::WAIT_ROUTE, 0);
    park->park(32, frame(2), NetwSpawnPark::WAIT_SCENE, 500);
    park->park(33, frame(3), NetwSpawnPark::WAIT_SCENE, 900);

    PackedInt64Array on_route;
    on_route.push_back(31);
    CHECK(park->waiting_on(NetwSpawnPark::WAIT_ROUTE) == on_route);
    NETW_CHECK_EQ(park->waiting_on(NetwSpawnPark::WAIT_SCENE).size(), 2);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] only a scene wait expires on the wall clock, "
    "because a route wait is bounded by its waiter"
) {
    const Ref<NetwSpawnPark> park = fresh();
    park->park(31, frame(1), NetwSpawnPark::WAIT_ROUTE, 0);
    park->park(32, frame(2), NetwSpawnPark::WAIT_SCENE, 500);

    CHECK_FALSE(park->is_expired(32, 499));
    CHECK(park->is_expired(32, 500));
    CHECK(park->is_expired(32, 5000));

    CHECK_FALSE(park->is_expired(31, 5000));
    CHECK_FALSE(park->is_expired(99, 5000));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a cleared park is waiting for nothing"
) {
    const Ref<NetwSpawnPark> park = fresh();
    park->park(31, frame(1), NetwSpawnPark::WAIT_ROUTE, 0);
    park->park(32, frame(2), NetwSpawnPark::WAIT_SCENE, 500);

    park->clear();

    NETW_CHECK_EQ(park->size(), 0);
    CHECK_FALSE(park->has(31));
    NETW_CHECK_EQ(park->waiting_on(NetwSpawnPark::WAIT_SCENE).size(), 0);
}

TEST_CASE("[Networked][Spawn][Hosted] an anchor parks only where its route is "
          "unresolvable, never where it is merely lingering") {
    CHECK(NetwSpawnPark::anchor_parks(netw::NetwLivenessCore::STATE_UNKNOWN));
    CHECK(NetwSpawnPark::anchor_parks(netw::NetwLivenessCore::STATE_DEAD));
    CHECK_FALSE(NetwSpawnPark::anchor_parks(netw::NetwLivenessCore::STATE_LIVE));
    CHECK_FALSE(
        NetwSpawnPark::anchor_parks(netw::NetwLivenessCore::STATE_LINGERING)
    );
}

} // namespace TestNetwSpawnPark
