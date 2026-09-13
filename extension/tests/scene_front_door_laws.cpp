#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestSceneFrontDoorLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> authority_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->clock_engine().set_configured(true);
    core->session_get_inner()->set_multiplayer_peer(Ref<MultiplayerPeer>());
    core->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] FD1 the front door names no path and answers "
    "at once, because a change with nothing to change to is refused before "
    "authority is even consulted"
) {
    const Ref<NetwMultiplayer> core = authority_core();

    const Ref<netw::NetwPromise> empty = core->scene_front_door_change(
        nullptr,
        String(),
        NetwMultiplayer::SCENE_CHANGE_SESSION
    );

    CHECK(empty.is_valid());
    CHECK(empty->get_is_settled());
    NETW_CHECK_EQ(empty->get_code(), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(core->get_scene_core()->get_pending_request_id(), 0);
}

} // namespace TestSceneFrontDoorLaws
