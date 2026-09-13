#include "support/netw_test.h"

#include "netw/rate_window.hpp"

namespace TestRateWindow {

using namespace godot;
using netw::RateWindow;

constexpr int64_t NOW = 1'000'000;
constexpr int64_t SPAN = 1000;
constexpr int LIMIT = 8;
constexpr int64_t STEADY_GAP = 200;
constexpr int STEADY_STEPS = 60;

bool spending_whole_budget_refuses_the_next(
    RateWindow &p_window,
    int p_peer,
    int64_t p_now
) {
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        if (p_window.exceeded(p_peer, p_now)) {
            return true;
        }
    }
    return p_window.exceeded(p_peer, p_now);
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W1 the budget admits its whole width and "
    "refuses the one past it"
) {
    RateWindow window;
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        CHECK_FALSE(window.exceeded(7, NOW));
    }
    CHECK(window.exceeded(7, NOW));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W2 the span expires, which is the half no "
    "wall clock could prove"
) {
    RateWindow window;
    CHECK(spending_whole_budget_refuses_the_next(window, 7, NOW));
    CHECK(window.exceeded(7, NOW + SPAN - 1));
    CHECK_FALSE(window.exceeded(7, NOW + SPAN + 1));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W3 the span slides rather than resetting"
) {
    RateWindow window;
    bool refused = false;
    for (int step = 0; step < STEADY_STEPS; ++step) {
        refused
            = refused || window.exceeded(7, NOW + int64_t(step) * STEADY_GAP);
    }
    CHECK_FALSE(refused);
}

TEST_CASE("[Networked][RateWindow][Hosted] W4 the budget is per peer") {
    RateWindow window;
    CHECK(spending_whole_budget_refuses_the_next(window, 7, NOW));
    CHECK_FALSE(window.exceeded(8, NOW));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W5 the exempt peer is never counted"
) {
    RateWindow window;

    SUBCASE("the peer named exempt spends forever") {
        bool refused = false;
        for (int attempt = 0; attempt < 40; ++attempt) {
            refused = refused || window.exceeded(window.exempt_peer, NOW);
        }
        CHECK_FALSE(refused);
    }

    SUBCASE("naming a peer nobody holds exempts no one") {
        window.exempt_peer = 0;
        CHECK(spending_whole_budget_refuses_the_next(window, 1, NOW));
    }
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W6 an idle peer is pruned and a busy one "
    "survives the prune"
) {
    RateWindow window;
    const int tracked = window.tracked_peers;
    for (int peer = 2; peer < tracked + 4; ++peer) {
        window.exceeded(peer, NOW);
    }

    SUBCASE("a peer whose spend aged out is dropped and spends again") {
        CHECK(
            spending_whole_budget_refuses_the_next(window, 3, NOW + 5 * SPAN)
        );
    }

    SUBCASE("the peer whose request triggered the prune survives it") {
        CHECK(
            spending_whole_budget_refuses_the_next(window, 3, NOW + 5 * SPAN)
        );
        for (int peer = 2; peer < tracked + 4; ++peer) {
            window.exceeded(peer, NOW + 5 * SPAN);
        }
        CHECK(window.exceeded(3, NOW + 5 * SPAN));
    }
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W7 the budget and the span are settings"
) {
    RateWindow window;
    window.limit = 2;
    window.span_msec = 50;

    CHECK_FALSE(window.exceeded(7, NOW));
    CHECK_FALSE(window.exceeded(7, NOW));
    CHECK(window.exceeded(7, NOW));
    CHECK_FALSE(window.exceeded(7, NOW + 51));
}

TEST_CASE(
    "[Networked][RateWindow][Hosted] W8 clearing forgets every peer's spend"
) {
    RateWindow window;
    CHECK(spending_whole_budget_refuses_the_next(window, 7, NOW));
    window.clear();
    CHECK_FALSE(window.exceeded(7, NOW));
}

} // namespace TestRateWindow
