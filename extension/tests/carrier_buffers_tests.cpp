#include "support/netw_test.h"

#include "netw/carrier_buffers.hpp"

using namespace godot;

namespace TestNetwCarrierBuffers {

using godot::PackedByteArray;
using godot::Ref;
using netw::CarrierBatch;
using netw::NetwCarrierBuffers;

constexpr int64_t PEER = 7;
constexpr int64_t BUDGET = 100;

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
    NetwCarrierBuffers held;
    NetwCarrierBuffers *const buffers = &held;

    CHECK(buffers->append(PEER, frame(40, 1), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);
    CHECK(buffers->append(PEER, frame(40, 2), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 80);

    const CarrierBatch owed
        = buffers->append(PEER, frame(40, 3), false, BUDGET);
    NETW_CHECK_EQ(owed.size(), 80);
    if (owed.size() == 80) {
        NETW_CHECK_EQ(owed.bytes()[0], 1);
        NETW_CHECK_EQ(owed.bytes()[40], 2);
    }
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);

    const CarrierBatch rest = buffers->take(PEER, false);
    NETW_CHECK_EQ(rest.size(), 40);
    if (rest.size() == 40) {
        NETW_CHECK_EQ(rest.bytes()[0], 3);
    }
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A2 a frame that alone exceeds the budget "
    "rides rather than being refused"
) {
    NetwCarrierBuffers held;
    NetwCarrierBuffers *const buffers = &held;

    CHECK(buffers->append(PEER, frame(400, 9), false, BUDGET).is_empty());
    NETW_CHECK_EQ(buffers->pending(PEER, false), 400);

    const CarrierBatch owed
        = buffers->append(PEER, frame(10, 8), false, BUDGET);
    NETW_CHECK_EQ(owed.size(), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 10);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A3 the reliable lane has no budget, and the "
    "two lanes are separate datagrams"
) {
    NetwCarrierBuffers held;
    NetwCarrierBuffers *const buffers = &held;

    for (int i = 0; i < 10; ++i) {
        CHECK(buffers->append(PEER, frame(40, 1), true, BUDGET).is_empty());
    }
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 0);

    buffers->append(PEER, frame(40, 2), false, BUDGET);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
    NETW_CHECK_EQ(buffers->pending(PEER, false), 40);

    NETW_CHECK_EQ(buffers->take(PEER, false).size(), 40);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 400);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A4 a run is one peer's, and taking it "
    "empties "
    "the lane"
) {
    NetwCarrierBuffers held;
    NetwCarrierBuffers *const buffers = &held;
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

    CHECK(buffers->take(99, false).is_empty());
    NETW_CHECK_EQ(buffers->peers(false).size(), 1);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] A5 clearing drops both lanes, which is what "
    "a session ending is"
) {
    NetwCarrierBuffers held;
    NetwCarrierBuffers *const buffers = &held;
    buffers->append(PEER, frame(10, 1), false, BUDGET);
    buffers->append(PEER, frame(10, 2), true, BUDGET);

    buffers->clear();

    NETW_CHECK_EQ(buffers->pending(PEER, false), 0);
    NETW_CHECK_EQ(buffers->pending(PEER, true), 0);
    NETW_CHECK_EQ(buffers->peers(false).size(), 0);
    NETW_CHECK_EQ(buffers->peers(true).size(), 0);
}

} // namespace TestNetwCarrierBuffers
