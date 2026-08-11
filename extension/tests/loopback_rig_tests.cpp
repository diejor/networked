// What the rig is, proven against a real session rather than described.
//
// These cases exist so a family crossing later can trust the substrate it
// writes against: that a rig stands up N connected sessions with no tree and no
// frame, that its clock only moves when something moves it, and that the link
// it conditions is the link the sessions actually talk over.
//
// Tier 2a only, and deliberately not [Hosted]: the rig constructs its sessions
// from a script under res://, which the module tier cannot see.

#include "support/netw_test.h"

#include "support/loopback_rig.h"

namespace TestNetwLoopbackRig {

using namespace godot;
using netw::LocalLinkConditions;
using netw_test::LoopbackRig;

int connected_count(Object *p_api) {
    return int(Array(p_api->call("get_peers")).size());
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
    CHECK(Array(rig.server()->call("get_peers")).has(rig.peer_id(0)));
    CHECK(Array(rig.server()->call("get_peers")).has(rig.peer_id(1)));

    // The loopback supports server relay, so a client learns of its siblings
    // as well as of the server, and never of itself.
    const Array seen = rig.client(0)->call("get_peers");
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
    CHECK(Array(rig.server()->call("get_peers")).has(rig.peer_id(added)));
}

TEST_CASE(
    "[Networked][Transport] A rig conditions the link its sessions really "
    "talk over"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    Ref<LocalLinkConditions> slow = LocalLinkConditions::create(3);
    slow->set_latency_ms(3.0 * 1000.0 / 60.0);
    // Conditions are installed on the RECEIVER, so this is the server's inbound
    // edge and what the client sends is what waits.
    rig.conditions(-1, slow);

    PackedByteArray payload;
    payload.push_back(0x7);
    rig.client(0)->call("send_bytes", payload, 1);

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
    rig.server()->call("send_bytes", payload, rig.peer_id(0));
    rig.pump(2);
    NETW_CHECK_EQ(rig.peer(0)->get_available_packet_count(), 0);

    rig.release(0);
    CHECK_FALSE(rig.session()->is_holding_inbound(rig.peer(0)));
    CHECK(rig.peer(0)->get_available_packet_count() > 0);
}

} // namespace TestNetwLoopbackRig
