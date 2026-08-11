// The per-peer rate window's laws.
//
// One rule with two consumers: the session admits joins under it and the scene
// column admits change requests under it. Both used to carry their own copy of
// it, byte for byte, down to the same three constants, so a fix to either was a
// fix to one of them.
//
// The window's own edge is the half no GDScript suite could ever prove. Both
// copies read the wall clock, so a case could count a flood and could never
// watch one expire, and the suite that named itself "drops past the window"
// only ever checked the count. The clock is an argument here.

#include "support/netw_test.h"

#include "netw/rate_window.hpp"

namespace TestNetwRateWindow {

using namespace godot;
using netw::NetwRateWindow;

constexpr int64_t NOW = 1'000'000;
constexpr int64_t SPAN = 1000;
constexpr int LIMIT = 8;

Ref<NetwRateWindow> fresh() {
    Ref<NetwRateWindow> window;
    window.instantiate();
    return window;
}

// Spends a peer's whole budget at one instant and answers whether the next
// request at that same instant is refused.
bool spend_budget(
    const Ref<NetwRateWindow> &p_window,
    int p_peer,
    int64_t p_now
) {
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        if (p_window->exceeded(p_peer, p_now)) {
            return true;
        }
    }
    return p_window->exceeded(p_peer, p_now);
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W1 the budget admits its whole width and "
    "refuses the one past it"
) {
    Ref<NetwRateWindow> window = fresh();
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        CHECK_FALSE(window->exceeded(7, NOW));
    }
    CHECK(window->exceeded(7, NOW));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W2 the span expires, which is the half no "
    "wall clock could prove"
) {
    Ref<NetwRateWindow> window = fresh();
    CHECK(spend_budget(window, 7, NOW));
    CHECK(window->exceeded(7, NOW + SPAN - 1));
    CHECK_FALSE(window->exceeded(7, NOW + SPAN + 1));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W3 the span slides rather than resetting"
) {
    Ref<NetwRateWindow> window = fresh();
    // One request every 200 ms is five per second, under a budget of eight, so
    // a peer asking steadily forever is never refused.
    bool refused = false;
    for (int step = 0; step < 60; ++step) {
        refused = refused || window->exceeded(7, NOW + int64_t(step) * 200);
    }
    CHECK_FALSE(refused);
}

TEST_CASE("[Networked][RateWindow][Hosted] W4 the budget is per peer") {
    Ref<NetwRateWindow> window = fresh();
    CHECK(spend_budget(window, 7, NOW));
    CHECK_FALSE(window->exceeded(8, NOW));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W5 the exempt peer is never counted"
) {
    Ref<NetwRateWindow> window = fresh();
    bool refused = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        refused = refused || window->exceeded(window->get_exempt_peer(), NOW);
    }
    CHECK_FALSE(refused);

    // Exemption is a setting rather than a constant, so a window guarding
    // something the host is not authoritative over can drop it.
    window->set_exempt_peer(0);
    CHECK(spend_budget(window, 1, NOW));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W6 an idle peer is pruned and a busy one "
    "survives the prune"
) {
    Ref<NetwRateWindow> window = fresh();
    const int tracked = window->get_tracked_peers();
    for (int peer = 2; peer < tracked + 4; ++peer) {
        window->exceeded(peer, NOW);
    }
    CHECK(spend_budget(window, 3, NOW + 5 * SPAN));

    // The prune only runs past the cap, and it must never drop the peer whose
    // requests are what triggered it.
    for (int peer = 2; peer < tracked + 4; ++peer) {
        window->exceeded(peer, NOW + 5 * SPAN);
    }
    CHECK(window->exceeded(3, NOW + 5 * SPAN));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W7 the budget and the span are settings"
) {
    Ref<NetwRateWindow> window = fresh();
    window->set_limit(2);
    window->set_span_msec(50);

    CHECK_FALSE(window->exceeded(7, NOW));
    CHECK_FALSE(window->exceeded(7, NOW));
    CHECK(window->exceeded(7, NOW));
    CHECK_FALSE(window->exceeded(7, NOW + 51));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W8 clearing forgets every peer's spend"
) {
    Ref<NetwRateWindow> window = fresh();
    CHECK(spend_budget(window, 7, NOW));
    window->clear();
    CHECK_FALSE(window->exceeded(7, NOW));
}

} // namespace TestNetwRateWindow
