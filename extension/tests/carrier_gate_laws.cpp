#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/carrier_frame.hpp"

namespace TestCarrierGate {

using namespace godot;
using netw::NetwCarrierFrame;
using netw::NetwMultiplayer;
using netw_test::CallLog;

constexpr int64_t USER_CHANNEL = 100;

PackedByteArray body(uint8_t fill) {
    PackedByteArray out;
    out.push_back(fill);
    return out;
}

PackedByteArray datagram(int64_t route, int64_t seq, uint8_t fill) {
    const PackedByteArray frame = NetwMultiplayer::frame_pack(
        route,
        0,
        USER_CHANNEL,
        body(fill),
        String()
    );
    return NetwCarrierFrame::build(frame, false, seq, -1, 0, -1);
}

TEST_CASE(
    "[Networked][Carrier][Hosted] CG1 route zero is the session's own stream "
    "and rides ungated, so a reordered datagram still reaches its channel"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog log;
    core->rpc_channel_register(USER_CHANNEL, log.callable("channel"), false);

    core->session_on_inner_packet(1, datagram(0, 900, 1));
    core->session_on_inner_packet(1, datagram(0, 400, 2));
    core->session_on_inner_packet(1, datagram(0, 900, 3));

    NETW_CHECK_EQ(log.count("channel"), 3);
}

} // namespace TestCarrierGate
