// One datagram's frames on the way in: what a refusal costs the rest of them.
//
// The decision these pin is where the barrier sits. Per row it is absolute, a
// row that fails halfway writes none of itself. Per DATAGRAM it must not be,
// because frames in one datagram address different entities and refusing all
// of them over one bad frame is a stall a sender can arrange for free.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/receive_pass.hpp"

namespace TestNetwReplReceivePass {

using godot::Error;
using godot::LocalVector;
using netw::repl::AdmitFn;
using netw::repl::ApplyFn;
using netw::repl::InboundFrame;
using netw::repl::ReceivePass;
using netw::repl::ReceiveResult;

struct Log {
    LocalVector<int64_t> admitted;
    LocalVector<int64_t> applied;
    int64_t refuse_route = -1;
    int64_t fail_apply_route = -1;
};

Error admit(const InboundFrame &p_frame, void *p_context) {
    Log *log = static_cast<Log *>(p_context);
    log->admitted.push_back(p_frame.route);
    if (p_frame.route == log->refuse_route) {
        return Error::ERR_UNAUTHORIZED;
    }
    return Error::OK;
}

Error apply(const InboundFrame &p_frame, void *p_context) {
    Log *log = static_cast<Log *>(p_context);
    if (p_frame.route == log->fail_apply_route) {
        return Error::ERR_INVALID_DATA;
    }
    log->applied.push_back(p_frame.route);
    return Error::OK;
}

InboundFrame frame(int64_t p_route) {
    InboundFrame out;
    out.channel = 19;
    out.route = p_route;
    out.sender = 2;
    out.payload_empty = false;
    return out;
}

LocalVector<InboundFrame> datagram(int count) {
    LocalVector<InboundFrame> out;
    for (int64_t at = 1; at <= count; ++at) {
        out.push_back(frame(at));
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] frames apply in the order they arrived"
) {
    Log log;
    const LocalVector<InboundFrame> frames = datagram(4);
    const ReceiveResult result
        = ReceivePass::run(frames, admit, apply, &log);

    NETW_CHECK_EQ(result.admitted, 4);
    NETW_CHECK_EQ(result.refused, 0);
    REQUIRE(log.applied.size() == 4);

    // Nothing sorts, buckets or defers between frames. Two runs over one
    // datagram apply the same frames in the same order, which is the whole
    // basis on which a capture can be replayed and compared.
    for (uint32_t at = 0; at < log.applied.size(); ++at) {
        NETW_CHECK_EQ(log.applied[at], int64_t(at) + 1);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a refused frame never reaches the applier"
) {
    Log log;
    log.refuse_route = 2;
    const LocalVector<InboundFrame> frames = datagram(3);
    const ReceiveResult result
        = ReceivePass::run(frames, admit, apply, &log);

    NETW_CHECK_EQ(result.refused, 1);
    NETW_CHECK_EQ(result.verdicts[1], Error::ERR_UNAUTHORIZED);

    // The gate exists to refuse work, so running it after the read would have
    // already spent what it was meant to save.
    REQUIRE(log.applied.size() == 2);
    NETW_CHECK_EQ(log.applied[0], 1);
    NETW_CHECK_EQ(log.applied[1], 3);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one bad frame does not stall the entities that "
    "shared its datagram"
) {
    Log log;
    log.fail_apply_route = 2;
    const LocalVector<InboundFrame> frames = datagram(4);
    const ReceiveResult result
        = ReceivePass::run(frames, admit, apply, &log);

    // Frames in one datagram address different entities. Refusing all of them
    // over one would let a sender stall every unrelated entity by malforming a
    // single frame, which costs the sender nothing.
    NETW_CHECK_EQ(result.admitted, 3);
    NETW_CHECK_EQ(result.refused, 1);
    REQUIRE(log.applied.size() == 3);
    NETW_CHECK_EQ(log.applied[0], 1);
    NETW_CHECK_EQ(log.applied[1], 3);
    NETW_CHECK_EQ(log.applied[2], 4);
}

TEST_CASE(
    "[Networked][Repl][Hosted] every frame gets a verdict, in arrival order"
) {
    Log log;
    log.refuse_route = 1;
    log.fail_apply_route = 3;
    const LocalVector<InboundFrame> frames = datagram(4);
    const ReceiveResult result
        = ReceivePass::run(frames, admit, apply, &log);

    // A caller that cannot say WHICH frame was refused cannot attribute a drop
    // to a sender, so the verdicts are positional rather than a tally.
    REQUIRE(result.verdicts.size() == 4);
    NETW_CHECK_EQ(result.verdicts[0], Error::ERR_UNAUTHORIZED);
    NETW_CHECK_EQ(result.verdicts[1], Error::OK);
    NETW_CHECK_EQ(result.verdicts[2], Error::ERR_INVALID_DATA);
    NETW_CHECK_EQ(result.verdicts[3], Error::OK);
    NETW_CHECK_EQ(result.admitted, 2);
    NETW_CHECK_EQ(result.refused, 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an empty datagram is not a refusal"
) {
    Log log;
    const LocalVector<InboundFrame> none;
    const ReceiveResult result
        = ReceivePass::run(none, admit, apply, &log);

    NETW_CHECK_EQ(result.verdicts.size(), 0);
    NETW_CHECK_EQ(result.admitted, 0);
    NETW_CHECK_EQ(result.refused, 0);
}

} // namespace TestNetwReplReceivePass
