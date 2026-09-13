#include "support/netw_test.h"

#include "support/loopback_rig.h"

namespace TestNetwLoopbackRig {

using namespace godot;
using netw::LocalLinkConditions;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

int connected_count(NetwMultiplayer *p_api) {
    return int(p_api->get_peers().size());
}

TEST_CASE(
    "[Networked][Transport] A rig stands up a server and its clients with no "
    "tree and no frame"
) {
    LoopbackRig rig(2);

    NETW_CHECK_EQ(rig.count(), 2);
    CHECK(rig.server() != nullptr);
    CHECK(rig.client(0) != nullptr);
    CHECK(rig.client(1) != nullptr);

    rig.pump(4);

    NETW_CHECK_EQ(connected_count(rig.server()), 2);
    CHECK(rig.server()->get_peers().has(rig.peer_id(0)));
    CHECK(rig.server()->get_peers().has(rig.peer_id(1)));

    const PackedInt32Array seen = rig.client(0)->get_peers();
    NETW_CHECK_EQ(seen.size(), 2);
    CHECK(seen.has(1));
    CHECK(seen.has(rig.peer_id(1)));
    CHECK_FALSE(seen.has(rig.peer_id(0)));
}

TEST_CASE(
    "[Networked][Transport] Rig peer ids are drawn, not counted, and differ "
    "per client"
) {
    LoopbackRig rig(3);

    CHECK(rig.peer_id(0) != rig.peer_id(1));
    CHECK(rig.peer_id(1) != rig.peer_id(2));
    CHECK(rig.peer_id(0) != rig.peer_id(2));
    for (int index = 0; index < rig.count(); ++index) {
        CHECK(rig.peer_id(index) > 1);
    }
}

TEST_CASE(
    "[Networked][Transport] A client added later reaches the server too"
) {
    LoopbackRig rig(1);
    rig.pump(4);
    NETW_CHECK_EQ(connected_count(rig.server()), 1);

    const int added = rig.add_client();
    NETW_CHECK_EQ(added, 1);
    rig.pump(4);

    NETW_CHECK_EQ(connected_count(rig.server()), 2);
    CHECK(rig.server()->get_peers().has(rig.peer_id(added)));
}

TEST_CASE(
    "[Networked][Transport] A rig conditions the link its sessions really "
    "talk over"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    Ref<LocalLinkConditions> slow = LocalLinkConditions::create(3);
    slow->set_latency_ms(3.0 * 1000.0 / 60.0);
    rig.conditions(-1, slow);

    PackedByteArray payload;
    payload.push_back(0x7);
    rig.client(0)
        ->send_bytes(payload, 1, MultiplayerPeer::TRANSFER_MODE_RELIABLE, 0);

    NETW_CHECK_EQ(rig.session()->in_flight_count(rig.peer(-1)), 1);
    rig.pump(3);
    NETW_CHECK_EQ(rig.session()->in_flight_count(rig.peer(-1)), 0);
}

TEST_CASE("[Networked][Transport] A held client releases what it was holding") {
    LoopbackRig rig(1);
    rig.pump(4);

    rig.hold(0);
    CHECK(rig.session()->is_holding_inbound(rig.peer(0)));

    PackedByteArray payload;
    payload.push_back(0x9);
    rig.server()->send_bytes(
        payload,
        rig.peer_id(0),
        MultiplayerPeer::TRANSFER_MODE_RELIABLE,
        0
    );
    rig.pump(2);
    NETW_CHECK_EQ(rig.peer(0)->get_available_packet_count(), 0);

    rig.release(0);
    CHECK_FALSE(rig.session()->is_holding_inbound(rig.peer(0)));
    CHECK(rig.peer(0)->get_available_packet_count() > 0);
}

} // namespace TestNetwLoopbackRig
