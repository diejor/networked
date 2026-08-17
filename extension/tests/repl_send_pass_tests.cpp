// One datagram's send, where the fitter and the ack book have to agree.
//
// Each has its own laws. What only a pass can be held to is that what went out
// is what was recorded: a frame counted as sent and not tracked is a frame
// whose loss is never reported, and a datagram tracked but not sent leaves the
// book waiting on something that will never be acked. Both read as healthy
// from inside either component.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/send_pass.hpp"

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

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("pass_probe");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

FitCandidate offer(int64_t send_id, int64_t bits) {
    FitCandidate out;
    out.channel_id = CHANNEL;
    out.payload_bits = bits;
    out.priority = 1.0f;
    out.accumulated_priority = 1.0f;
    out.send_id = send_id;
    return out;
}

LocalVector<FitCandidate> offers(int count, int64_t bits) {
    LocalVector<FitCandidate> out;
    for (int64_t at = 1; at <= count; ++at) {
        out.push_back(offer(at, bits));
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] what rides the datagram is what the book is "
    "holding"
) {
    const WireRegistry reg = registry();
    SendPass pass;
    LocalVector<FitCandidate> queue = offers(3, 300);

    const PassResult result = pass.run(reg, queue, 1000, 1, 5000);
    NETW_CHECK_EQ(result.sent.size(), 3);
    NETW_CHECK_EQ(result.deferred.size(), 0);
    NETW_CHECK_EQ(result.sent_bits, 900);
    CHECK_FALSE(result.untrackable);

    // One entry for the datagram, not one per frame. An echo names a datagram,
    // so a datagram is the finest thing an ack can settle and the book is
    // shaped to match.
    NETW_CHECK_EQ(pass.outstanding(), 1);

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(1, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(delivered[0].send_id, 5000);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a pass that carries nothing records nothing"
) {
    const WireRegistry reg = registry();
    SendPass pass;

    LocalVector<FitCandidate> empty;
    const PassResult nothing = pass.run(reg, empty, 1000, 1, 5000);
    NETW_CHECK_EQ(nothing.sent.size(), 0);
    CHECK_FALSE(nothing.untrackable);

    // A datagram nobody filled must not occupy a ring slot, or a quiet lane
    // would exhaust the book with empty sends and then refuse the first real
    // one.
    NETW_CHECK_EQ(pass.outstanding(), 0);

    LocalVector<FitCandidate> over = offers(1, 2000);
    const PassResult refused = pass.run(reg, over, 1000, 2, 5001);
    NETW_CHECK_EQ(refused.sent.size(), 0);
    NETW_CHECK_EQ(refused.deferred.size(), 1);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a datagram the book cannot hold is not sent at "
    "all"
) {
    const WireRegistry reg = registry();
    SendPass pass;

    LocalVector<FitCandidate> first = offers(2, 300);
    const PassResult held = pass.run(reg, first, 1000, 1, 5000);
    REQUIRE(held.sent.size() == 2);

    // Seq 129 collides with seq 1 in a 128-slot ring while seq 1 is still
    // live, so the book refuses it.
    LocalVector<FitCandidate> colliding = offers(2, 300);
    const PassResult refused = pass.run(reg, colliding, 1000, 129, 5001);

    // Nothing rides. Sending untracked would put bytes on the wire that no ack
    // can settle and no loss report can name, so the frames wait for a seq the
    // book can hold instead.
    CHECK(refused.untrackable);
    NETW_CHECK_EQ(refused.sent.size(), 0);
    NETW_CHECK_EQ(refused.sent_bits, 0);
    NETW_CHECK_EQ(refused.deferred.size(), 2);
    NETW_CHECK_EQ(pass.outstanding(), 1);

    // And the frames that waited are owed more than they were, so they are not
    // stuck behind whatever arrives fresh next pass.
    for (uint32_t at = 0; at < refused.deferred.size(); ++at) {
        CHECK(refused.deferred[at].accumulated_priority
              > refused.deferred[at].priority);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a refused datagram rides the next seq the book "
    "can hold"
) {
    const WireRegistry reg = registry();
    SendPass pass;

    LocalVector<FitCandidate> first = offers(1, 300);
    REQUIRE(pass.run(reg, first, 1000, 1, 5000).sent.size() == 1);

    LocalVector<FitCandidate> colliding = offers(1, 300);
    const PassResult refused = pass.run(reg, colliding, 1000, 129, 5001);
    REQUIRE(refused.untrackable);

    LocalVector<FitCandidate> retry;
    for (uint32_t at = 0; at < refused.deferred.size(); ++at) {
        retry.push_back(refused.deferred[at]);
    }
    const PassResult later = pass.run(reg, retry, 1000, 2, 5002);
    CHECK_FALSE(later.untrackable);
    NETW_CHECK_EQ(later.sent.size(), 1);
    NETW_CHECK_EQ(pass.outstanding(), 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] what the budget deferred and what the book "
    "deferred both come back"
) {
    const WireRegistry reg = registry();
    SendPass pass;

    LocalVector<FitCandidate> first = offers(1, 300);
    REQUIRE(pass.run(reg, first, 1000, 1, 5000).sent.size() == 1);

    // Four offers of 300 bits against a 1000-bit budget: three fit and one
    // does not. The seq then collides, so the three the budget took are
    // deferred too and every offer has to come back.
    LocalVector<FitCandidate> queue = offers(4, 300);
    const PassResult result = pass.run(reg, queue, 1000, 129, 5001);

    CHECK(result.untrackable);
    NETW_CHECK_EQ(result.sent.size(), 0);
    NETW_CHECK_EQ(result.deferred.size(), 4);
}

} // namespace TestNetwReplSendPass
