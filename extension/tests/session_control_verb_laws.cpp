#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestSessionControlVerbLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

Ref<NetwMultiplayer> authority() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->clock_engine().set_configured(true);
    core->session_get_inner()->set_multiplayer_peer(Ref<MultiplayerPeer>());
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

TEST_CASE(
    "[Networked][Session][Hosted] SC1 a pause is a broadcast authority hears "
    "with everyone else, so the host that issued it stops on the same edge "
    "its clients do rather than on a local shortcut of its own"
) {
    const Ref<NetwMultiplayer> core = authority();
    const CallLog seen;
    core->connect(StringName("session_tree_paused"), seen.callable("paused"));
    core->connect(
        StringName("session_tree_unpaused"),
        seen.callable("unpaused")
    );

    core->session_pause(String("waiting"));

    NETW_CHECK_EQ(seen.count("paused"), 1);
    const Array reason = seen.args("paused");
    NETW_CHECK_EQ(reason.size(), 1);
    if (reason.size() != 1) {
        return;
    }
    NETW_CHECK_EQ(int(String(reason[0]) == String("waiting")), 1);

    core->session_unpause();

    NETW_CHECK_EQ(seen.count("unpaused"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] SC2 a kick asked for on authority is "
    "published as the request it is rather than performed, so the same "
    "listener answers a host's own ask and a client's, naming who asked and "
    "who was named"
) {
    const Ref<NetwMultiplayer> core = authority();
    const CallLog seen;
    core->connect(StringName("peer_kick_requested"), seen.callable("kick"));

    core->peer_request_kick(9, String("griefing"));

    NETW_CHECK_EQ(seen.count("kick"), 1);
    const Array named = seen.args("kick");
    NETW_CHECK_EQ(named.size(), 3);
    if (named.size() != 3) {
        return;
    }
    NETW_CHECK_EQ(int(named[0]), 1);
    NETW_CHECK_EQ(int(named[1]), 9);
    NETW_CHECK_EQ(int(String(named[2]) == String("griefing")), 1);
}

} // namespace TestSessionControlVerbLaws
