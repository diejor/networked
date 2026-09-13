#include "support/netw_test.h"

#include "netw/session_core.hpp"
#include "support/loopback_rig.h"

namespace TestNetwSessionRig {

using namespace godot;
using netw::SessionCore;
using netw_test::LoopbackRig;

TEST_CASE(
    "[Networked][Session] R1 a real server peer carries a real session to "
    "online at the assignment edge, and defaults to a listen server"
) {
    LoopbackRig rig(0);
    Object *server = rig.server();
    REQUIRE(server != nullptr);

    NETW_CHECK_EQ(int(server->get("state")), int(SessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(
        int(server->get("role")),
        int(SessionCore::ROLE_LISTEN_SERVER)
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

    NETW_CHECK_EQ(int(client->get("state")), int(SessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(int(client->get("role")), int(SessionCore::ROLE_CLIENT));
}

} // namespace TestNetwSessionRig
