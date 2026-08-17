// What the fitter does with more frames than a datagram holds.
//
// The fitter is the only place that decides what is sent NOW and what waits,
// so two of its properties are load-bearing beyond packing well. A deferred
// candidate has to rise, or a channel that loses one pass loses every pass and
// starves behind a busier one. And the order has to be a function of the
// candidates alone, or two runs over identical input pack differently and
// every byte-exact instrument downstream is measuring the sort.
//
// The sibling suite covers that packing stays inside the budget. This covers
// the decisions either side of that: what happens to what did not fit, and
// what happens when two candidates are equally entitled.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwWireFitterLaws {

using godot::LocalVector;
using netw::wire::Delivery;
using netw::wire::FitCandidate;
using netw::wire::FitResult;
using netw::wire::WireFitter;
using netw::wire::WireRegistry;

const uint8_t FITTED_CHANNEL = 40;

WireRegistry fitted_registry() {
    WireRegistry registry;
    netw::wire::ChannelDecl decl;
    decl.id = FITTED_CHANNEL;
    decl.name = godot::StringName("fitted_probe");
    decl.delivery = Delivery::FITTED;
    registry.register_channel(decl);
    return registry;
}

FitCandidate candidate(int64_t send_id, int64_t bits, float priority = 1.0f) {
    FitCandidate out;
    out.channel_id = FITTED_CHANNEL;
    out.payload_bits = bits;
    out.priority = priority;
    out.accumulated_priority = priority;
    out.send_id = send_id;
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] a candidate that did not fit is owed more than "
    "it was"
) {
    const WireRegistry registry = fitted_registry();
    LocalVector<FitCandidate> candidates;
    candidates.push_back(candidate(1, 800));
    candidates.push_back(candidate(2, 800));

    const FitResult result = WireFitter::fit(registry, candidates, 1000);
    NETW_CHECK_EQ(result.packed.size(), 1);
    NETW_CHECK_EQ(result.deferred.size(), 1);

    // The deferred candidate leaves owed its own priority again. Without that
    // it arrives at the next pass exactly as entitled as it was, loses to the
    // same rival, and starves for as long as the rival keeps sending.
    CHECK(result.deferred[0].accumulated_priority
          > result.deferred[0].priority);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a starved candidate eventually outranks the "
    "one that kept beating it"
) {
    const WireRegistry registry = fitted_registry();
    LocalVector<FitCandidate> candidates;
    candidates.push_back(candidate(1, 800, 4.0f));
    candidates.push_back(candidate(2, 800, 1.0f));

    // The loser is carried forward as the fitter left it, which is what a
    // caller does with a deferral, and the winner arrives fresh each pass.
    FitCandidate starved;
    int won_at = -1;
    for (int pass = 0; pass < 6; ++pass) {
        LocalVector<FitCandidate> round;
        round.push_back(candidate(1, 800, 4.0f));
        if (pass == 0) {
            round.push_back(candidate(2, 800, 1.0f));
        } else {
            round.push_back(starved);
        }
        const FitResult result = WireFitter::fit(registry, round, 1000);
        REQUIRE(result.packed.size() == 1);
        if (result.packed[0].send_id == 2) {
            won_at = pass;
            break;
        }
        REQUIRE(result.deferred.size() == 1);
        starved = result.deferred[0];
    }
    CHECK(won_at >= 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] the budget admits an exact fit and refuses one "
    "bit past it"
) {
    const WireRegistry registry = fitted_registry();

    LocalVector<FitCandidate> exact;
    exact.push_back(candidate(1, 1000));
    const FitResult fits = WireFitter::fit(registry, exact, 1000);
    NETW_CHECK_EQ(fits.packed.size(), 1);
    NETW_CHECK_EQ(fits.total_bits, 1000);

    LocalVector<FitCandidate> over;
    over.push_back(candidate(1, 1001));
    const FitResult refused = WireFitter::fit(registry, over, 1000);
    NETW_CHECK_EQ(refused.packed.size(), 0);
    NETW_CHECK_EQ(refused.deferred.size(), 1);
    NETW_CHECK_EQ(refused.total_bits, 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] equally entitled candidates pack in the order "
    "they were offered"
) {
    const WireRegistry registry = fitted_registry();
    LocalVector<FitCandidate> candidates;
    // Past the threshold where a introsort stops falling back to insertion
    // sort, since below it an unstable sort is incidentally stable and the law
    // would pass on the implementation rather than on the contract.
    for (int64_t id = 1; id <= 64; ++id) {
        candidates.push_back(candidate(id, 100));
    }

    // Every candidate here is equally entitled, so nothing about them decides
    // the order and only the offer order is left. An unstable sort would be
    // free to answer differently for the same input, and every byte-exact
    // instrument downstream would then be measuring the sort rather than the
    // lane.
    const FitResult result = WireFitter::fit(registry, candidates, 400);
    REQUIRE(result.packed.size() == 4);
    for (uint32_t at = 0; at < result.packed.size(); ++at) {
        NETW_CHECK_EQ(result.packed[at].send_id, int64_t(at) + 1);
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted] one offer answers the same way twice"
) {
    const WireRegistry registry = fitted_registry();
    LocalVector<FitCandidate> first;
    LocalVector<FitCandidate> second;
    for (int64_t id = 1; id <= 8; ++id) {
        first.push_back(candidate(id, 100, 2.0f));
        second.push_back(candidate(id, 100, 2.0f));
    }

    const FitResult a = WireFitter::fit(registry, first, 350);
    const FitResult b = WireFitter::fit(registry, second, 350);
    REQUIRE(a.packed.size() == b.packed.size());
    for (uint32_t at = 0; at < a.packed.size(); ++at) {
        NETW_CHECK_EQ(a.packed[at].send_id, b.packed[at].send_id);
    }
}

} // namespace TestNetwWireFitterLaws
