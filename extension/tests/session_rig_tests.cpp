// A real session against the machine inside it.
//
// The cases beside this file reason about the machine from facts. These reason
// about a real session: the rig hands a real LocalMultiplayerPeer to a real
// api, so what they cover that no fact-driven case can is the interface's own
// reading of the peer, which is the half of the session that did not cross.
//
// Tier 2a only, and deliberately not [Hosted]: the rig constructs its sessions
// from a script under res://, which the module tier cannot see.

#include "support/netw_test.h"

#include "netw/session_core.hpp"
#include "support/loopback_rig.h"

namespace TestNetwSessionRig {

using namespace godot;
using netw::NetwSessionCore;
using netw_test::LoopbackRig;

TEST_CASE(
    "[Networked][Session] R1 a real server peer carries a real session to "
    "online at the assignment edge"
) {
    LoopbackRig rig(0);
    Object *server = rig.server();
    REQUIRE(server != nullptr);

    NETW_CHECK_EQ(
        int(server->get("state")),
        int(NetwSessionCore::STATE_ONLINE)
    );
    // Nothing configured a role, and the default intent is to play, so a host
    // that never said otherwise is a listen server.
    NETW_CHECK_EQ(
        int(server->get("role")),
        int(NetwSessionCore::ROLE_LISTEN_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session] R2 a real client reaches online on the transport's "
    "own signal and reads as a client"
) {
    LoopbackRig rig(1);
    Object *client = rig.client(0);
    REQUIRE(client != nullptr);
    rig.pump(4);

    NETW_CHECK_EQ(
        int(client->get("state")),
        int(NetwSessionCore::STATE_ONLINE)
    );
    NETW_CHECK_EQ(int(client->get("role")), int(NetwSessionCore::ROLE_CLIENT));
}

} // namespace TestNetwSessionRig
