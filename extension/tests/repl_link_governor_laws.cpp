#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/loopback.hpp"
#include "netw/repl/link_governor.hpp"

using namespace godot;

namespace TestNetwReplLinkGovernor {

using godot::Ref;
using netw::LocalLinkConditions;
using netw::repl::LinkGovernor;

const int PEER = 5;

void drive(
    LinkGovernor &r_link,
    const Ref<LocalLinkConditions> &p_link,
    int p_datagrams,
    int64_t p_from_tick
) {
    const double rtt_ms = p_link->get_latency_ms() * 2.0;
    const double jitter_ms = p_link->get_jitter_ms();
    const double loss = p_link->get_packet_loss();
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

void settle(LinkGovernor &r_link, double p_rtt_ms, int64_t p_from_tick) {
    for (int at = 0; at < int(LinkGovernor::CLEAN_CAP) + 64; ++at) {
        r_link.note_ack(PEER, 1, 0, p_rtt_ms, 0.0, p_from_tick + at);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a healthy link leaves the governor in GOOD and "
    "the budget whole"
) {
    LinkGovernor link;
    drive(link, LocalLinkConditions::wifi(), int(LinkGovernor::WINDOW) * 2, 0);

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
        LocalLinkConditions::mobile_4g(),
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
    drive(link, LocalLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);

    const bool it_went_bad = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(it_went_bad);
    NETW_CHECK_EQ(link.budget_bits(PEER, 8000), int64_t(4000));
}

TEST_CASE("[Networked][Repl][Hosted] the budget never halves below the floor") {
    LinkGovernor link;
    drive(link, LocalLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);

    REQUIRE(link.mode(PEER) == LinkGovernor::BAD);
    NETW_CHECK_EQ(
        link.budget_bits(PEER, LinkGovernor::FLOOR_BITS),
        LinkGovernor::FLOOR_BITS
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that recovers returns to GOOD only "
    "after a clean interval"
) {
    LinkGovernor link;
    drive(link, LocalLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);
    REQUIRE(link.mode(PEER) == LinkGovernor::BAD);

    const double calm_rtt = LocalLinkConditions::wifi()->get_latency_ms() * 2.0;
    link.note_ack(PEER, 1, 0, calm_rtt, 0.0, 1000);
    const bool one_clean_ack_is_not_enough
        = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(one_clean_ack_is_not_enough);

    settle(link, calm_rtt, 1001);
    const bool it_came_back = link.mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_came_back);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a link that relapses inside the flap window "
    "owes a longer clean interval than the first time"
) {
    LinkGovernor link;
    const double calm_rtt = LocalLinkConditions::wifi()->get_latency_ms() * 2.0;

    drive(link, LocalLinkConditions::poor_3g(), int(LinkGovernor::WINDOW), 0);
    REQUIRE(link.mode(PEER) == LinkGovernor::BAD);
    settle(link, calm_rtt, 100);
    REQUIRE(link.mode(PEER) == LinkGovernor::GOOD);

    drive(
        link,
        LocalLinkConditions::poor_3g(),
        int(LinkGovernor::WINDOW),
        2000
    );
    REQUIRE(link.mode(PEER) == LinkGovernor::BAD);

    for (int at = 0; at < int(LinkGovernor::CLEAN_TICKS) + 2; ++at) {
        link.note_ack(PEER, 1, 0, calm_rtt, 0.0, 3000 + at);
    }
    const bool the_first_interval_no_longer_suffices
        = link.mode(PEER) == LinkGovernor::BAD;
    CHECK(the_first_interval_no_longer_suffices);
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
