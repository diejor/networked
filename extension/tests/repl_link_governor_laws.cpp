#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/loopback.hpp"
#include "netw/repl/link_governor.hpp"

using namespace godot;

namespace TestNetwReplLinkGovernor {

using godot::Ref;
using netw::NetwLinkConditions;
using netw::repl::LinkGovernor;

const int PEER = 5;

void drive(
    LinkGovernor &r_link,
    const Ref<NetwLinkConditions> &p_link,
    int p_datagrams,
    int64_t p_from_tick
) {
    const double rtt_ms = p_link->get_latency_ms() * 2.0;
    const double jitter_ms = p_link->get_jitter_ms();
    const double loss = p_link->get_loss_chance();
    double owed = 0.0;
    for (int at = 0; at < p_datagrams; ++at) {
        owed += loss;
        const bool lost = owed >= 1.0;
        if (lost) {
            owed -= 1.0;
        }
        r_link.note_ack(
            PEER,
            lost ? 0 : 1,
            lost ? 1 : 0,
            rtt_ms,
            jitter_ms,
            p_from_tick + at
        );
    }
}

double share_after_clean_round(
    LinkGovernor &r_link,
    double p_rtt_ms,
    int64_t &r_now_ms
) {
    r_now_ms += int64_t(p_rtt_ms) + 1;
    r_link.note_ack(PEER, 1, 0, p_rtt_ms, 0.0, r_now_ms);
    return double(r_link.share(PEER));
}

struct CappedLink {
    double capacity = 0.6;
    double owed = 0.0;

    void tick(LinkGovernor &r_link, int64_t p_now_ms) {
        const double share = double(r_link.share(PEER));
        const double excess
            = share > capacity ? (share - capacity) / share : 0.0;
        owed += excess;
        const bool lost = owed >= 1.0;
        if (lost) {
            owed -= 1.0;
        }
        r_link.note_ack(PEER, lost ? 0 : 1, lost ? 1 : 0, 50.0, 0.0, p_now_ms);
    }
};

TEST_CASE(
    "[Networked][Repl][Hosted] a healthy link leaves the governor in GOOD and "
    "the budget whole"
) {
    LinkGovernor link;
    drive(link, NetwLinkConditions::wifi(), int(LinkGovernor::WINDOW) * 2, 0);

    const bool it_stayed_good = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_stayed_good);
    NETW_CHECK_EQ(link.budget_bits(PEER, 8000), int64_t(8000));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a four-g link is not what the governor is for"
) {
    LinkGovernor link;
    drive(
        link,
        NetwLinkConditions::mobile_4g(),
        int(LinkGovernor::WINDOW) * 2,
        0
    );

    const bool it_stayed_good = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_stayed_good);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a poor 3g link enters BAD within one window "
    "and the volatile budget halves"
) {
    LinkGovernor link;
    drive(link, NetwLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);

    const bool it_went_bad = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(it_went_bad);
    NETW_CHECK_EQ(link.budget_bits(PEER, 8000), int64_t(4000));
}

TEST_CASE("[Networked][Repl][Hosted] the budget never halves below the floor") {
    LinkGovernor link;
    drive(link, NetwLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);

    REQUIRE(link.mode(PEER) == LinkGovernor::BAD);
    NETW_CHECK_EQ(
        link.budget_bits(PEER, LinkGovernor::FLOOR_BITS),
        LinkGovernor::FLOOR_BITS
    );
}

const double CALM_RTT_MS = 30.0;
const double EXACT = 1e-6;

void lose_a_round(LinkGovernor &r_link, int64_t &r_now_ms) {
    r_now_ms += int64_t(CALM_RTT_MS) + 1;
    r_link.note_ack(
        PEER,
        0,
        LinkGovernor::MIN_SAMPLES,
        CALM_RTT_MS,
        0.0,
        r_now_ms
    );
}

void climb(LinkGovernor &r_link, int p_rounds, int64_t &r_now_ms) {
    for (int round = 0; round < p_rounds; ++round) {
        share_after_clean_round(r_link, CALM_RTT_MS, r_now_ms);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that recovers from one lossy episode "
    "adds back an eighth of its rate per clean round trip until it is whole"
) {
    LinkGovernor link;
    int64_t now_ms = 0;
    lose_a_round(link, now_ms);
    const bool it_went_bad = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(it_went_bad);
    NETW_CHECK_CLOSE(link.share(PEER), 0.5, EXACT);

    NETW_CHECK_CLOSE(
        share_after_clean_round(link, CALM_RTT_MS, now_ms),
        0.625,
        EXACT
    );
    NETW_CHECK_CLOSE(
        share_after_clean_round(link, CALM_RTT_MS, now_ms),
        0.75,
        EXACT
    );
    NETW_CHECK_CLOSE(
        share_after_clean_round(link, CALM_RTT_MS, now_ms),
        0.875,
        EXACT
    );
    NETW_CHECK_CLOSE(
        share_after_clean_round(link, CALM_RTT_MS, now_ms),
        1.0,
        EXACT
    );
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a loss burst lasting several round trips "
    "halves the rate each round and leaves no ceiling behind it"
) {
    LinkGovernor link;
    int64_t now_ms = 0;
    lose_a_round(link, now_ms);
    lose_a_round(link, now_ms);
    lose_a_round(link, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 0.125, EXACT);

    climb(link, 16, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 1.0, EXACT);
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that loses again after climbing back "
    "stops one step under the rate that lost until the hold expires"
) {
    LinkGovernor link;
    int64_t now_ms = 0;
    climb(link, int(LinkGovernor::WINDOW), now_ms);
    lose_a_round(link, now_ms);
    climb(link, 4, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 1.0, EXACT);

    lose_a_round(link, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 0.5, EXACT);
    climb(link, 16, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 0.875, EXACT);
    const bool still_bad_under_the_ceiling
        = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(still_bad_under_the_ceiling);

    now_ms += LinkGovernor::CEILING_HOLD_MS;
    climb(link, 1, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 1.0, EXACT);
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that loses twice before it has carried "
    "one window climbs back whole, because a new link sets no ceiling"
) {
    LinkGovernor link;
    int64_t now_ms = 0;
    lose_a_round(link, now_ms);
    climb(link, 4, now_ms);
    lose_a_round(link, now_ms);
    climb(link, 16, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 1.0, EXACT);
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a round trip that spikes without loss halves "
    "the rate once and leaves no ceiling behind it"
) {
    LinkGovernor link;
    int64_t now_ms = 0;
    climb(link, int(LinkGovernor::WINDOW), now_ms);
    now_ms += 16;
    link.note_ack(
        PEER,
        1,
        0,
        CALM_RTT_MS + LinkGovernor::RTT_EXCESS_MS * 2.0,
        0.0,
        now_ms
    );
    NETW_CHECK_CLOSE(link.share(PEER), 0.5, EXACT);

    climb(link, 8, now_ms);
    NETW_CHECK_CLOSE(link.share(PEER), 1.0, EXACT);
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that loses above a fixed rate settles "
    "within one step of it and stays there for thirty seconds"
) {
    LinkGovernor link;
    CappedLink capped;
    int64_t now_ms = 0;
    for (int tick = 0; tick < 600; ++tick) {
        now_ms += 16;
        capped.tick(link, now_ms);
    }
    double least = 1.0;
    double most = 0.0;
    for (int tick = 0; tick < 30 * 60; ++tick) {
        now_ms += 16;
        capped.tick(link, now_ms);
        const double share = double(link.share(PEER));
        least = share < least ? share : least;
        most = share > most ? share : most;
    }
    const double step = double(LinkGovernor::STEP);
    NETW_CHECK_GE(least, capped.capacity - step);
    NETW_CHECK_LE(most, capped.capacity + step);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer the governor has never heard from is "
    "GOOD with its whole budget"
) {
    LinkGovernor link;
    const bool an_unknown_peer_is_good = link.mode(999) == LinkGovernor::GOOD;
    CHECK(an_unknown_peer_is_good);
    NETW_CHECK_EQ(link.budget_bits(999, 8000), int64_t(8000));
    NETW_CHECK_EQ(double(link.loss(999)), 0.0);
}

} // namespace TestNetwReplLinkGovernor
