// The carrier's aggregation laws.
//
// Every case here is about the boundary between one datagram and the next,
// because that is the only thing this class decides. The shell version made
// the same decision at its call site, where the budget read, the flush and the
// re-fetch of the emptied run were three statements a caller had to keep in
// that order; the one that mattered was the re-fetch, since appending to the
// stale run would have put the frame back into the datagram just sent.

#include "support/netw_test.h"

#include "netw/carrier_buffers.hpp"

using namespace godot;

namespace TestNetwCarrierBuffers {

using godot::PackedByteArray;
using godot::Ref;
using netw::NetwCarrierBuffers;

constexpr int64_t PEER = 7;
constexpr int64_t BUDGET = 100;

Ref<NetwCarrierBuffers> fresh() {
    Ref<NetwCarrierBuffers> buffers;
    buffers.instantiate();
    return buffers;
}

PackedByteArray frame(int p_size, uint8_t p_fill) {
    PackedByteArray out;
    out.resize(p_size);
    for (int i = 0; i < p_size; ++i) {
        out.set(i, p_fill);
    }
    return out;
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A1 frames accumulate into one run until the "
    "budget stops them"
) {
    Ref<NetwCarrierBuffers> buffers = fresh();

    CHECK(buffers->append(PEER, frame(40, 1), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);
    CHECK(buffers->append(PEER, frame(40, 2), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 80);

    // The third would take the run past the budget, so the first two go out
    // and the third opens the next datagram rather than joining theirs.
    const PackedByteArray owed = buffers->append(PEER, frame(40, 3), false, BUDGET);
    NETW_CHECK_EQ(owed.size(), 80);
    if (owed.size() == 80) {
        NETW_CHECK_EQ(owed[0], 1);
        NETW_CHECK_EQ(owed[40], 2);
    }
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);

    // The handed-back run must not still be in the lane, or the frame just
    // sent rides again in the next datagram.
    const PackedByteArray rest = buffers->take(PEER, false);
    NETW_CHECK_EQ(rest.size(), 40);
    if (rest.size() == 40) {
        NETW_CHECK_EQ(rest[0], 3);
    }
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A2 a frame that alone exceeds the budget "
    "rides rather than being refused"
) {
    Ref<NetwCarrierBuffers> buffers = fresh();

    // Nothing is owed, because there is nothing held to owe. Refusing here
    // would drop a frame the caller has no other way to send.
    CHECK(buffers->append(PEER, frame(400, 9), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 400);

    // And it does not swallow the next frame with it: the oversized run is
    // handed back exactly once.
    const PackedByteArray owed = buffers->append(PEER, frame(10, 8), false, BUDGET);
    NETW_CHECK_EQ(owed.size(), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 10);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A3 the reliable lane has no budget, and the "
    "two lanes are separate datagrams"
) {
    Ref<NetwCarrierBuffers> buffers = fresh();

    for (int i = 0; i < 10; ++i) {
        CHECK(buffers->append(PEER, frame(40, 1), true, BUDGET).is_empty());
    }
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 0);

    buffers->append(PEER, frame(40, 2), false, BUDGET);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);

    // Taking one lane leaves the other standing, because they are two
    // datagrams and a flush of one is not a flush of both.
    NETW_CHECK_EQ(buffers->take(PEER, false).size(), 40);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A4 a run is one peer's, and taking it empties "
    "the lane"
) {
    Ref<NetwCarrierBuffers> buffers = fresh();
    buffers->append(3, frame(10, 1), false, BUDGET);
    buffers->append(1, frame(10, 2), false, BUDGET);
    buffers->append(2, frame(10, 3), true, BUDGET);

    const godot::PackedInt32Array unreliable = buffers->peers(false);
    NETW_CHECK_EQ(unreliable.size(), 2);
    if (unreliable.size() == 2) {
        NETW_CHECK_EQ(unreliable[0], 1);
        NETW_CHECK_EQ(unreliable[1], 3);
    }
    NETW_CHECK_EQ(buffers->peers(true).size(), 1);

    NETW_CHECK_EQ(buffers->take(3, false).size(), 10);
    NETW_CHECK_EQ(buffers->pending(3, false), 0);
    NETW_CHECK_EQ(buffers->peers(false).size(), 1);

    // A peer that never buffered anything answers an empty run rather than
    // opening one.
    CHECK(buffers->take(99, false).is_empty());
    NETW_CHECK_EQ(buffers->peers(false).size(), 1);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A5 clearing drops both lanes, which is what "
    "a session ending is"
) {
    Ref<NetwCarrierBuffers> buffers = fresh();
    buffers->append(PEER, frame(10, 1), false, BUDGET);
    buffers->append(PEER, frame(10, 2), true, BUDGET);

    buffers->clear();

    NETW_CHECK_EQ(buffers->pending(PEER, false), 0);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 0);
    NETW_CHECK_EQ(buffers->peers(false).size(), 0);
    NETW_CHECK_EQ(buffers->peers(true).size(), 0);
}

} // namespace TestNetwCarrierBuffers
