#include "support/netw_test.h"

#include "netw/probe_guard.hpp"

namespace TestNetwProbeGuard {

TEST_CASE(
    "[Networked][Session][Hosted] P1 ten probes inside one second are "
    "admitted and the eleventh is refused"
) {
    netw::ProbeGuard guard;
    for (int at = 0; at < netw::ProbeGuard::RATE_PER_SECOND; at++) {
        CHECK(guard.admit(int64_t(2000 + at), 10000));
    }
    CHECK_FALSE(guard.admit(9000, 10000));
    NETW_CHECK_EQ(guard.window_count(), 11);
}

TEST_CASE(
    "[Networked][Session][Hosted] P2 a probe a full second after the ones "
    "that filled the window is admitted again"
) {
    netw::ProbeGuard guard;
    for (int at = 0; at < netw::ProbeGuard::RATE_PER_SECOND + 1; at++) {
        guard.admit(int64_t(2000 + at), 10000);
    }
    CHECK_FALSE(guard.admit(3000, 10999));
    CHECK(guard.admit(3001, 12000));
    NETW_CHECK_EQ(guard.window_count(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] P3 a tracked peer is forgotten once, and "
    "a peer that never probed is not"
) {
    netw::ProbeGuard guard;
    guard.admit(77, 500);
    NETW_CHECK_EQ(guard.active_count(), 1);
    CHECK(guard.forget(77));
    CHECK_FALSE(guard.forget(77));
    CHECK_FALSE(guard.forget(78));
    NETW_CHECK_EQ(guard.active_count(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] P4 a peer beyond the tracked maximum is "
    "refused however slowly it probes"
) {
    netw::ProbeGuard guard;
    int64_t now = 0;
    for (int at = 0; at < netw::ProbeGuard::MAX_ACTIVE; at++) {
        now += netw::ProbeGuard::WINDOW_MS + 1;
        CHECK(guard.admit(int64_t(100 + at), now));
    }
    now += netw::ProbeGuard::WINDOW_MS + 1;
    CHECK_FALSE(guard.admit(999, now));
    NETW_CHECK_EQ(guard.active_count(), netw::ProbeGuard::MAX_ACTIVE + 1);
    NETW_CHECK_EQ(guard.window_count(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] P5 clearing drops both the tracked peers "
    "and the rate window"
) {
    netw::ProbeGuard guard;
    for (int at = 0; at < netw::ProbeGuard::RATE_PER_SECOND + 1; at++) {
        guard.admit(int64_t(2000 + at), 10000);
    }
    guard.clear();
    NETW_CHECK_EQ(guard.active_count(), 0);
    NETW_CHECK_EQ(guard.window_count(), 0);
    CHECK(guard.admit(2000, 10000));
}

} // namespace TestNetwProbeGuard
