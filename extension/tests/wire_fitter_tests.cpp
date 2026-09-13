#include "support/netw_test.h"

#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwWireFitter {

using netw::wire::FitCandidate;
using netw::wire::FitResult;
using netw::wire::WireFitter;
using netw::wire::WireRegistry;

TEST_CASE(
    "[Networked][Wire][Hosted] WireFitter packs candidates within budget"
) {
    const WireRegistry reg = WireRegistry::create_default();
    godot::LocalVector<FitCandidate> candidates;

    FitCandidate c1;
    c1.channel_id = 19;
    c1.payload_bits = 200;
    c1.priority = 2.0f;
    c1.accumulated_priority = 2.0f;
    c1.send_id = 1;
    candidates.push_back(c1);

    FitCandidate c2;
    c2.channel_id = 19;
    c2.payload_bits = 400;
    c2.priority = 1.0f;
    c2.accumulated_priority = 1.0f;
    c2.send_id = 2;
    candidates.push_back(c2);

    FitCandidate c3;
    c3.channel_id = 19;
    c3.payload_bits = 350;
    c3.priority = 1.0f;
    c3.accumulated_priority = 1.0f;
    c3.send_id = 3;
    candidates.push_back(c3);

    const FitResult res = WireFitter::fit(reg, candidates, 500);
    NETW_CHECK_EQ(res.packed.size(), 1);
    NETW_CHECK_EQ(res.packed[0].send_id, 1);
    NETW_CHECK_EQ(res.deferred.size(), 2);
    NETW_CHECK_EQ(res.total_bits, 200);

    const bool the_deferred_are_owed_more
        = res.deferred[0].accumulated_priority > res.deferred[0].priority;
    CHECK(the_deferred_are_owed_more);
}

} // namespace TestNetwWireFitter
