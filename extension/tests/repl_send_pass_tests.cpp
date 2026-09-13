#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/send_pass.hpp"

using namespace godot;

namespace TestNetwReplSendPass {

using godot::LocalVector;
using netw::repl::PassResult;
using netw::repl::SendPass;
using netw::wire::AckEntry;
using netw::wire::ChannelDecl;
using netw::wire::Delivery;
using netw::wire::FitCandidate;
using netw::wire::WireRegistry;

const uint8_t CHANNEL = 42;
const int PEER = 7;
const int OTHER = 9;

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("pass_probe");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

FitCandidate offer(int peer, int64_t send_id, int64_t bits) {
    FitCandidate out;
    out.channel_id = CHANNEL;
    out.peer = peer;
    out.payload_bits = bits;
    out.priority = 1.0f;
    out.accumulated_priority = 1.0f;
    out.send_id = send_id;
    return out;
}

LocalVector<FitCandidate> offers(int peer, int count, int64_t bits) {
    LocalVector<FitCandidate> out;
    for (int64_t at = 1; at <= count; ++at) {
        out.push_back(offer(peer, at, bits));
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a pass packs what the budget holds and defers "
    "the rest"
) {
    const WireRegistry reg = registry();
    SendPass pass;
    LocalVector<FitCandidate> queue = offers(PEER, 4, 300);

    const PassResult result = pass.run(reg, PEER, queue, 1000);
    NETW_CHECK_EQ(result.sent.size(), 3);
    NETW_CHECK_EQ(result.deferred.size(), 1);
    NETW_CHECK_EQ(result.sent_bits, 900);

    const bool the_deferred_row_is_owed_more
        = result.deferred[0].accumulated_priority > result.deferred[0].priority;
    CHECK(the_deferred_row_is_owed_more);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a pass records nothing until the carrier names "
    "the seq"
) {
    const WireRegistry reg = registry();
    SendPass pass;
    LocalVector<FitCandidate> queue = offers(PEER, 3, 300);

    pass.run(reg, PEER, queue, 10000);
    NETW_CHECK_EQ(pass.outstanding(), 0);

    REQUIRE(pass.record_datagram(PEER, 1, 3, 900));
    NETW_CHECK_EQ(pass.outstanding(), 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 1, 0, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(delivered[0].frames, 3);
    NETW_CHECK_EQ(delivered[0].bits, 900);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one peer's book never answers for another's"
) {
    const WireRegistry reg = registry();
    SendPass pass;

    LocalVector<FitCandidate> mine = offers(PEER, 1, 300);
    LocalVector<FitCandidate> theirs = offers(OTHER, 1, 300);
    pass.run(reg, PEER, mine, 10000);
    pass.run(reg, OTHER, theirs, 10000);

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));
    REQUIRE(pass.record_datagram(OTHER, 1, 1, 300));
    NETW_CHECK_EQ(pass.outstanding(), 2);

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 1, 0, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 0);
    NETW_CHECK_EQ(pass.outstanding(OTHER), 1);

    pass.forget(OTHER);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the delivery history settles the datagrams the "
    "ack itself does not name"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 8, 1, 300));
    REQUIRE(pass.record_datagram(PEER, 9, 1, 300));
    REQUIRE(pass.record_datagram(PEER, 10, 1, 300));

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    const uint32_t eight_arrived_nine_did_not = 0x2;
    pass.acknowledge(PEER, 10, eight_arrived_nine_did_not, delivered, lost);

    NETW_CHECK_EQ(delivered.size(), 2);
    NETW_CHECK_EQ(lost.size(), 0);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a datagram the history can no longer reach is "
    "reported lost"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 40, 0, delivered, lost);

    NETW_CHECK_EQ(delivered.size(), 0);
    NETW_CHECK_EQ(lost.size(), 1);
    NETW_CHECK_EQ(lost[0].seq, 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a seq whose slot is still live is not recorded "
    "over"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));

    ERR_PRINT_OFF;
    const bool the_colliding_seq_is_refused
        = !pass.record_datagram(PEER, 129, 1, 300);
    ERR_PRINT_ON;
    CHECK(the_colliding_seq_is_refused);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);
}

} // namespace TestNetwReplSendPass
