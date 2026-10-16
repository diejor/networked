#include "support/fault_log.h"
#include "support/netw_test.h"

#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/loopback_rig.h"

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

TEST_CASE("[Networked][Scene][Hosted] FD1 a server scene change needs a path") {
    const Ref<NetwMultiplayer> core = authority_core();

    const Ref<netw::NetwPromise> empty = core->scene_front_door_change(
        nullptr,
        String(),
        NetwMultiplayer::SCENE_CHANGE_SESSION
    );

    CHECK(empty.is_valid());
    CHECK(empty->get_is_settled());
    NETW_CHECK_EQ(empty->get_code(), int(ERR_UNAVAILABLE));
}

TEST_CASE(
    "[Networked][Scene][Hosted] clients cannot change or reload scenes at "
    "any scope"
) {
    netw_test::FaultWatch faults;
    faults.expect_error(
        "[error][scene] Only the server can change scenes. Call "
        "Netw.change_scene_to_file, Netw.change_scene_to_packed or "
        "Netw.reload_current_scene on the server.",
        9
    );
    const Ref<NetwMultiplayer> core = authority_core();
    core->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    for (int scope = NetwMultiplayer::SCENE_CHANGE_SESSION;
         scope <= NetwMultiplayer::SCENE_CHANGE_SCENE;
         ++scope) {
        const auto operation = NetwMultiplayer::SceneChange(scope);
        const Ref<netw::NetwPromise> refused[] = {
            core->scene_change_to_file(nullptr, String(), operation),
            core->scene_change_to_packed(
                nullptr,
                Ref<PackedScene>(),
                operation
            ),
            core->scene_reload_current(nullptr, operation),
        };
        for (const Ref<netw::NetwPromise> &promise : refused) {
            REQUIRE(promise.is_valid());
            CHECK(promise->get_is_failed());
            NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
        }
    }
    CHECK(core->live_scenes().is_empty());
    CHECK_FALSE(core->get_scene_core()->transition_busy());
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Scene][SceneTree] Netw refuses client scene changes before "
    "loading a destination or sending packets"
) {
    netw_test::LoopbackRig rig;
    rig.mount();
    Node *node = rig.client(0)->get_session()->get_root();
    REQUIRE(node != nullptr);
    netw_test::FaultWatch faults;
    faults.expect_error(
        "[error][scene] Only the server can change scenes. Call "
        "Netw.change_scene_to_file, Netw.change_scene_to_packed or "
        "Netw.reload_current_scene on the server.",
        15
    );
    Ref<PackedScene> unsaved;
    unsaved.instantiate();
    const Ref<PackedScene> saved = NetwMultiplayer::scene_packed_at(
        "res://tests/support/declared_level.tscn"
    );
    REQUIRE(saved.is_valid());
    const int delivered = rig.delivered_sends_at_server();
    for (int scope = NetwMultiplayer::SCENE_CHANGE_SESSION;
         scope <= NetwMultiplayer::SCENE_CHANGE_SCENE;
         ++scope) {
        const auto operation = NetwMultiplayer::SceneChange(scope);
        const Ref<netw::NetwPromise> refused[] = {
            netw::Netw::change_scene_to_file(
                node,
                saved->get_path(),
                operation
            ),
            netw::Netw::change_scene_to_packed(node, saved, operation),
            netw::Netw::change_scene_to_packed(node, unsaved, operation),
            netw::Netw::change_scene_to_packed(
                node,
                Ref<PackedScene>(),
                operation
            ),
            netw::Netw::reload_current_scene(node, operation),
        };
        for (const Ref<netw::NetwPromise> &promise : refused) {
            REQUIRE(promise.is_valid());
            CHECK(promise->get_is_failed());
            NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
        }
    }
    NETW_CHECK_EQ(rig.delivered_sends_at_server(), delivered);
    CHECK(rig.client(0)->live_scenes().is_empty());
    CHECK(rig.server()->live_scenes().is_empty());
}

#endif

} // namespace TestSceneFrontDoorLaws
