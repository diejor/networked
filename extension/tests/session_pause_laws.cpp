#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session/frames.hpp"
#include "netw/wire/registry.hpp"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionPauseLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::LoopbackRig;

struct TreeUnpausedAfter {
    ~TreeUnpausedAfter() {
        SceneTree *tree = netw::gd::scene_tree();
        if (tree != nullptr) {
            tree->set_pause(false);
        }
    }
};

bool tree_paused() {
    SceneTree *tree = netw::gd::scene_tree();
    return tree != nullptr && tree->is_paused();
}

void server_says_paused(NetwMultiplayer *p_core, bool p_paused) {
    if (p_paused) {
        p_core->session_publish_control(
            netw::wire::builtin_channel("SESSION_PAUSE"),
            MultiplayerPeer::TARGET_PEER_SERVER,
            netw::session::frame_write(netw::session::SessionReason{String()})
        );
        return;
    }
    p_core->session_publish_control(
        netw::wire::builtin_channel("SESSION_UNPAUSE"),
        MultiplayerPeer::TARGET_PEER_SERVER,
        PackedByteArray()
    );
}

int64_t ticks_over_frames(NetwMultiplayer *p_core, int p_frames) {
    const int64_t before = p_core->clock_engine().get_ticks_run();
    for (int frame = 0; frame < p_frames; ++frame) {
        p_core->clock_on_physics_frame();
    }
    return p_core->clock_engine().get_ticks_run() - before;
}

TEST_CASE(
    "[Networked][Session] a player who joins a paused session joins "
    "paused, and a second pause is not announced again"
) {
    const TreeUnpausedAfter restore;
    LoopbackRig rig(1);
    rig.pump(4);
    rig.join(-1, StringName("host"));

    const CallLog seen;
    rig.client(0)->observe(
        NetwMultiplayer::SESSION_TREE_PAUSED,
        seen.callable("early")
    );

    rig.server()->session_pause(String("menu"));
    rig.server()->session_pause(String("menu"));
    rig.pump(4);

    const int late = rig.add_client();
    rig.client(late)->observe(
        NetwMultiplayer::SESSION_TREE_PAUSED,
        seen.callable("late")
    );
    rig.pump(8);

    NETW_CHECK_EQ(int(rig.client(late)->session_is_paused()), 1);
    NETW_CHECK_EQ(seen.count("late"), 1);
    NETW_CHECK_EQ(seen.count("early"), 1);
    const Array reason = seen.args("late");
    NETW_CHECK_EQ(reason.size(), 1);
    if (reason.size() != 1) {
        return;
    }
    NETW_CHECK_EQ(int(String(reason[0]) == String("menu")), 1);
}

TEST_CASE(
    "[Networked][Session] a session that ends while paused unpauses the "
    "tree it paused"
) {
    const TreeUnpausedAfter restore;
    LoopbackRig rig(1);
    rig.pump(4);
    rig.join(-1, StringName("host"));

    const CallLog seen;
    rig.client(0)->observe(
        NetwMultiplayer::SESSION_TREE_UNPAUSED,
        seen.callable("client")
    );

    rig.server()->session_pause(String("menu"));
    rig.pump(4);
    NETW_CHECK_EQ(int(tree_paused()), 1);
    NETW_CHECK_EQ(int(rig.client(0)->session_is_paused()), 1);

    rig.close_link();
    rig.pump(8);

    NETW_CHECK_EQ(int(rig.client(0)->session_is_paused()), 0);
    NETW_CHECK_EQ(int(rig.server()->session_is_paused()), 0);
    NETW_CHECK_EQ(seen.count("client"), 1);
    NETW_CHECK_EQ(int(tree_paused()), 0);
}

TEST_CASE(
    "[Networked][Session] the tree stays paused while any session in the "
    "process is paused, and a pause the game set outlasts them all"
) {
    const TreeUnpausedAfter restore;
    Ref<NetwMultiplayer> first;
    first.instantiate();
    Ref<NetwMultiplayer> second;
    second.instantiate();

    server_says_paused(first.ptr(), true);
    server_says_paused(second.ptr(), true);
    server_says_paused(first.ptr(), false);
    NETW_CHECK_EQ(int(tree_paused()), 1);
    server_says_paused(second.ptr(), false);
    NETW_CHECK_EQ(int(tree_paused()), 0);

    netw::gd::scene_tree()->set_pause(true);
    server_says_paused(first.ptr(), true);
    server_says_paused(first.ptr(), false);
    NETW_CHECK_EQ(int(tree_paused()), 1);
}

TEST_CASE(
    "[Networked][Session] the clock keeps ticking through a SceneTree "
    "pause the game set, and stops for a session pause"
) {
    const TreeUnpausedAfter restore;
    LoopbackRig rig(1);
    rig.mount();
    Ref<netw::NetwClockConfig> clock;
    clock.instantiate();
    clock->set_tickrate(60);
    NETW_REQUIRE_EQ(rig.server()->clock_initialize(clock), OK);

    netw::gd::scene_tree()->set_pause(true);
    NETW_CHECK_EQ(int(ticks_over_frames(rig.server(), 4) > 0), 1);
    netw::gd::scene_tree()->set_pause(false);

    rig.server()->session_pause(String());
    NETW_CHECK_EQ(ticks_over_frames(rig.server(), 4), 0);
    rig.server()->session_unpause();
    NETW_CHECK_EQ(int(ticks_over_frames(rig.server(), 4) > 0), 1);
}

TEST_CASE(
    "[Networked][Session] a player kicked while the session is paused is "
    "disconnected without waiting for unpause"
) {
    const TreeUnpausedAfter restore;
    LoopbackRig rig(1);
    rig.pump(4);
    Ref<netw::NetwClockConfig> clock;
    clock.instantiate();
    NETW_REQUIRE_EQ(rig.server()->clock_initialize(clock), OK);
    rig.server()->clock_engine().set_manual_tick(true);
    rig.join(-1, StringName("host"));

    rig.server()->session_pause(String());
    rig.pump(4);
    const Ref<netw::NetwPlayer> alice
        = rig.server()->peer_get_player(rig.peer_id(0));
    NETW_REQUIRE_EQ(int(alice.is_valid()), 1);
    NETW_CHECK_EQ(int(rig.server()->player_kick(alice, String())), int(OK));
    rig.pump(8);

    NETW_CHECK_EQ(int(rig.client(0)->is_online()), 0);
}

} // namespace TestNetwSessionPauseLaws

#endif
