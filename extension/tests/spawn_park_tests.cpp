#include "support/netw_test.h"

#include "netw/liveness_core.hpp"
#include "netw/spawn/park.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPark {

using namespace godot;
using netw::spawn::Park;
using netw_test::CallLog;

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

    CHECK(park.park(31, frame(7), Park::WAIT_ROUTE, 0, 7));
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
    park.park(31, frame(7), Park::WAIT_ROUTE, 0, 7);

    CHECK_FALSE(park.park(31, frame(9), Park::WAIT_SPAWNER, 500, 9));

    NETW_CHECK_EQ(park.size(), 1);
    CHECK(park.take(31) == frame(7));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a cancelled park applies nothing, which is a "
    "despawn arriving mid-park"
) {
    Park park;
    park.park(31, frame(7), Park::WAIT_ROUTE, 0, 7);

    CHECK(park.cancel(31));
    CHECK_FALSE(park.cancel(31));
    NETW_CHECK_EQ(park.take(31).size(), 0);
    NETW_CHECK_EQ(park.size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] what a row waits on decides who retries it"
) {
    Park park;
    park.park(31, frame(1), Park::WAIT_ROUTE, 0, 7);
    park.park(32, frame(2), Park::WAIT_SPAWNER, 500, 7);
    park.park(33, frame(3), Park::WAIT_SPAWNER, 900, 7);

    PackedInt64Array on_route;
    on_route.push_back(31);
    CHECK(park.waiting_on(Park::WAIT_ROUTE) == on_route);
    NETW_CHECK_EQ(park.waiting_on(Park::WAIT_SPAWNER).size(), 2);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a wait expires once the count reaches its "
    "deadline, and a route that is not waiting never expires"
) {
    Park park;
    park.park(32, frame(2), Park::WAIT_SPAWNER, 500, 7);
    park.park(33, frame(3), Park::WAIT_ADOPT, 700, 7);

    CHECK_FALSE(park.is_expired(32, 499));
    CHECK(park.is_expired(32, 500));
    CHECK(park.is_expired(32, 5000));

    CHECK_FALSE(park.is_expired(33, 699));
    CHECK(park.is_expired(33, 700));

    CHECK_FALSE(park.is_expired(99, 5000));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a peeked payload stays parked, so a retry "
    "that does not land keeps the deadline it was parked under"
) {
    Park park;
    park.park(31, frame(7), Park::WAIT_ADOPT, 500, 7);

    CHECK(park.peek(31) == frame(7));
    CHECK(park.has(31));
    CHECK_FALSE(park.park(31, frame(9), Park::WAIT_ADOPT, 9000, 9));
    CHECK(park.is_expired(31, 500));

    CHECK(park.peek(404).is_empty());
}

TEST_CASE("[Networked][Spawn][Hosted] a cleared park is waiting for nothing") {
    Park park;
    park.park(31, frame(1), Park::WAIT_ROUTE, 0, 7);
    park.park(32, frame(2), Park::WAIT_SPAWNER, 500, 7);

    park.clear();

    NETW_CHECK_EQ(park.size(), 0);
    CHECK_FALSE(park.has(31));
    NETW_CHECK_EQ(park.waiting_on(Park::WAIT_SPAWNER).size(), 0);
}

enum ParkEnd {
    END_NONE,
    END_CANCEL,
    END_TAKE,
};

struct HeldRow {
    const char *name;
    int held_polls;
    ParkEnd end;
    int quiet_polls;
    int expired_after_quiet;
    int expired_one_poll_later;
};

TEST_CASE(
    "[Networked][Spawn][Hosted] LH2 LH4 LH5 a wait on a route whose spawn is "
    "parked does not age, ends with the park, and counts its deadline from "
    "the release"
) {
    const HeldRow rows[] = {
        {"LH2 a child parked on a held parent", 300, END_NONE, 0, 0, 0},
        {"LH4 the held spawn is cancelled by despawn", 10, END_CANCEL, 0, 1, 1},
        {"LH5 the held spawn is released without going live",
         300,
         END_TAKE,
         29,
         0,
         1},
    };
    for (const HeldRow &row : rows) {
        INFO(row.name);
        Ref<netw::NetwLivenessCore> liveness;
        liveness.instantiate();
        Park park;
        park.hold_routes_in(liveness);
        CallLog log;
        park.park(31, frame(7), Park::WAIT_SPAWNER, 0, 1);
        liveness->when_live(
            31,
            log.callable("live"),
            liveness->frame(),
            30,
            false,
            log.callable("expired")
        );

        for (int poll = 0; poll < row.held_polls; ++poll) {
            liveness->poll(0);
        }
        NETW_CHECK_EQ(log.count("expired"), 0);
        if (row.end == END_CANCEL) {
            park.cancel(31);
        } else if (row.end == END_TAKE) {
            park.take(31);
        }
        for (int poll = 0; poll < row.quiet_polls; ++poll) {
            liveness->poll(0);
        }
        NETW_CHECK_EQ(log.count("expired"), row.expired_after_quiet);
        liveness->poll(0);
        NETW_CHECK_EQ(log.count("expired"), row.expired_one_poll_later);

        liveness->flush_live(31);
        NETW_CHECK_EQ(log.count("live"), 1 - row.expired_one_poll_later);
    }
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
