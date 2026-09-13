#include "support/netw_test.h"

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/channel.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"

namespace TestNetwEntityChannelSignalLaws {

using namespace godot;
using netw::NetwChannel;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::ReplicationCore;
using netw_test::LoopbackRig;

constexpr int64_t CUSTOM_CHANNEL = 120;

TEST_CASE(
    "[Networked][Channel] a channel answers on the id it was asked for, "
    "because a game names its own lane and a session that hands back a "
    "different one silently merges two conversations"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = Object::cast_to<NetwMultiplayer>(rig.server());
    CHECK(core != nullptr);
    if (core == nullptr) {
        return;
    }
    const Ref<NetwChannel> lane = NetwChannel::over(CUSTOM_CHANNEL, core);
    CHECK(lane.is_valid());
    if (lane.is_valid()) {
        NETW_CHECK_EQ(lane->get_id(), CUSTOM_CHANNEL);
    }

    const Ref<NetwChannel> other = NetwChannel::over(CUSTOM_CHANNEL + 1, core);
    CHECK(other.is_valid());
    if (other.is_valid()) {
        NETW_CHECK_EQ(other->get_id(), CUSTOM_CHANNEL + 1);
    }

    CHECK(
        NetwChannel::over(NetwMultiplayer::CHANNEL_USER_FIRST - 1, core)
            .is_null()
    );
    CHECK(
        NetwChannel::over(NetwMultiplayer::CHANNEL_USER_LAST + 1, core)
            .is_null()
    );
}

TEST_CASE(
    "[Networked][Multiplayer] an entity signal send is charged the verdict "
    "the wire actually gave it, so a signal the node never declared is a "
    "refusal a watcher can read rather than a silent success"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = Object::cast_to<NetwMultiplayer>(rig.server());
    CHECK(core != nullptr);
    if (core == nullptr) {
        return;
    }
    Node *host = memnew(Node);
    host->set_name("SignalHost");
    rig.branch(-1)->add_child(host);

    const RID entity = core->entity_create();
    const int64_t route = core->entity_admit(entity);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(entity, host)), int(OK));

    ReplicationCore *plane = core->get_replication_plane();
    CHECK(plane != nullptr);
    if (plane == nullptr) {
        host->get_parent()->remove_child(host);
        memdelete(host);
        return;
    }

    const int64_t before = core->stats_get_verdict_count(ERR_INVALID_DATA);
    plane->send_signal(host, StringName("no_such_signal"), Array());
    NETW_CHECK_EQ(core->stats_get_verdict_count(ERR_INVALID_DATA), before + 1);

    plane->send_signal(host, StringName("tree_exited"), Array());
    NETW_CHECK_EQ(core->stats_get_verdict_count(ERR_INVALID_DATA), before + 1);

    host->get_parent()->remove_child(host);
    memdelete(host);
}

} // namespace TestNetwEntityChannelSignalLaws

#endif
