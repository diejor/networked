#include "support/netw_test.h"

#include "netw/wire/ack_book.hpp"
#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwWireFitter {

using netw::wire::AckBook;
using netw::wire::AckEntry;
using netw::wire::FitCandidate;
using netw::wire::FitResult;
using netw::wire::WireFitter;
using netw::wire::WireRegistry;

TEST_CASE(
    "[Networked][Wire][Hosted] AckBook tracks sends and reports delivered and lost"
) {
    AckBook book;
    book.record_send(1, 19, 1001);
    book.record_send(2, 19, 1002);
    book.record_send(3, 19, 1003);

    NETW_CHECK_EQ(book.active_count(), 3);

    godot::LocalVector<AckEntry> delivered;
    godot::LocalVector<AckEntry> lost;

    book.process_ack(2, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(delivered[0].send_id, 1002);
    NETW_CHECK_EQ(lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 2);

    book.process_ack(50, delivered, lost);
    NETW_CHECK_EQ(lost.size(), 2);
    NETW_CHECK_EQ(book.active_count(), 0);
}

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

    // Budget: 500 bits. Candidates c1 (200) + c2 (400) = 600 > 500.
    // c1 (200) fits. c2 (400) doesn't fit with c1.
    const FitResult res = WireFitter::fit(reg, candidates, 500);
    NETW_CHECK_EQ(res.packed.size(), 1);
    NETW_CHECK_EQ(res.packed[0].send_id, 1);
    NETW_CHECK_EQ(res.deferred.size(), 2);
    NETW_CHECK_EQ(res.total_bits, 200);

    // Deferred candidates accrued priority.
    CHECK(res.deferred[0].accumulated_priority > res.deferred[0].priority);
}

} // namespace TestNetwWireFitter
