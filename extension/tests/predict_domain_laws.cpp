// Which transitions a peer is entitled to reproduce exactly, and what writes
// that answer down. A prediction is comparable by fingerprint only when both
// peers ran against the same world, so the domain label carries that answer
// forward and the journal row retains it.

#include "support/netw_test.h"

#include "netw/predict/journal.hpp"
#include "netw/prediction_core.hpp"

using namespace godot;

namespace TestNetwPredictDomain {

using godot::Dictionary;
using godot::StringName;
using netw::predict::Domain;
using netw::predict::Journal;
using netw::predict::JournalOpen;

namespace prediction_core = netw::prediction_core;

constexpr int DOMAIN_IN = int(Domain::IN_DOMAIN);
constexpr int DOMAIN_OUT = int(Domain::OUT_OF_DOMAIN);

int declared_domain(int64_t p_label, int64_t p_window_until) {
    return prediction_core::domain_of(true, false, p_label, p_window_until);
}

Journal opened(int p_capacity, int64_t p_transition) {
    Journal journal(p_capacity);
    JournalOpen row;
    row.label = p_transition;
    row.c_hash = 1234;
    journal.open(p_transition, row);
    return journal;
}

TEST_CASE(
    "[Networked][Predict][Hosted] A contact covers its own transition and "
    "every tick of its cooldown"
) {
    const int64_t until = prediction_core::window_after(10, 3, -1);
    for (int64_t label = 10; label <= 13; ++label) {
        CAPTURE(label);
        NETW_CHECK_EQ(declared_domain(label, until), DOMAIN_OUT);
    }
    NETW_CHECK_EQ(declared_domain(14, until), DOMAIN_IN);
}

TEST_CASE("[Networked][Predict][Hosted] A later fact extends an open window") {
    const int64_t first = prediction_core::window_after(10, 2, -1);
    const int64_t extended = prediction_core::window_after(20, 2, first);
    NETW_CHECK_GT(extended, first);
    NETW_CHECK_EQ(declared_domain(21, extended), DOMAIN_OUT);
}

TEST_CASE(
    "[Networked][Predict][Hosted] The environment digest distinguishes which "
    "sensor read what"
) {
    Dictionary ground_low;
    ground_low[StringName("ground")] = 1.0;
    Dictionary ground_high;
    ground_high[StringName("ground")] = 1.5;
    CHECK(
        prediction_core::environment_digest(7, ground_low)
        != prediction_core::environment_digest(7, ground_high)
    );

    Dictionary forward;
    forward[StringName("a")] = 1.0;
    forward[StringName("b")] = 2.0;
    Dictionary swapped;
    swapped[StringName("a")] = 2.0;
    swapped[StringName("b")] = 1.0;
    CHECK(
        prediction_core::environment_digest(0, forward)
        != prediction_core::environment_digest(0, swapped)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted] Substitution downgrades the row it marks"
) {
    Journal journal = opened(8, 0);
    NETW_CHECK_EQ(int(journal.domain_of(0)), DOMAIN_IN);
    journal.mark_substituted(0);
    NETW_CHECK_EQ(int(journal.domain_of(0)), DOMAIN_OUT);
}

TEST_CASE(
    "[Networked][Predict][Hosted] A row records the environment it ran against"
) {
    Journal journal = opened(8, 3);
    journal.mark_e_digest(3, 99);
    journal.mark_domain(3, Domain::OUT_OF_DOMAIN);

    const int index = journal.index_of(3);
    REQUIRE(index >= 0);
    NETW_CHECK_EQ(journal.e_digest_at(index), 99);
    NETW_CHECK_EQ(int(journal.domain_at(index)), DOMAIN_OUT);
}

TEST_CASE(
    "[Networked][Predict][Hosted] A fact about an evicted row is dropped"
) {
    Journal journal(2);
    for (int64_t transition = 0; transition <= 2; ++transition) {
        JournalOpen row;
        row.label = transition;
        row.c_hash = int32_t(transition + 1);
        journal.open(transition, row);
    }

    journal.mark_domain(0, Domain::OUT_OF_DOMAIN);
    journal.mark_e_digest(0, 77);
    NETW_CHECK_LT(journal.index_of(0), 0);

    const int survivor = journal.index_of(2);
    REQUIRE(survivor >= 0);
    NETW_CHECK_EQ(int(journal.domain_at(survivor)), DOMAIN_IN);
    NETW_CHECK_EQ(journal.e_digest_at(survivor), 0);
}

} // namespace TestNetwPredictDomain
