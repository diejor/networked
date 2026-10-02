#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/replication_send.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

namespace TestStoreAccountSession {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

netw::ReplicationSend *row_send_of(NetwMultiplayer *p_core) {
    netw::ReplicationCore *plane = p_core->get_replication_plane();
    if (plane == nullptr) {
        return nullptr;
    }
    return plane->get_sync_pipeline()->row_send_under_test();
}

int64_t sent_to(netw::ReplicationSend *p_send, int64_t p_peer) {
    const netw::repl::PeerAccount *held = p_send->account(p_peer);
    return held != nullptr ? held->sent_bytes : 0;
}

TEST_CASE(
    "[Networked][Store][SceneTree] AS1 row control is charged to the account "
    "of the peer it is sent to"
) {
    LoopbackRig rig(1);
    rig.mount();
    rig.step_ticks(2);
    NetwMultiplayer *server = rig.server();
    netw::ReplicationSend *send = row_send_of(server);
    const bool armed = send != nullptr;
    CHECK(armed);
    if (send == nullptr) {
        return;
    }
    const int64_t peer = rig.peer_id(0);
    const int64_t channel
        = netw::wire::builtin_channel(StringName("ROW_CONTROL"));

    netw::wire::ControlRecord asking;
    asking.tag = netw::wire::ControlTag::OPEN;
    asking.request = netw::wire::next_open_request();
    asking.route = 90001;
    asking.ordinal = 0;
    asking.family = netw::wire::StreamFamily::VOLATILE;
    asking.epoch = 1;
    asking.schema = 7;
    const int64_t framed = netw::wire::frame_size(
        0,
        netw::wire::write_control_record(asking).size()
    );
    send->control_scheduler().queue(int(peer), asking);

    const int64_t before = sent_to(send, peer);
    server->row_control_flush(send, channel);
    const int64_t charged = sent_to(send, peer) - before;

    NETW_CHECK_GE(charged, framed);
}

} // namespace TestStoreAccountSession

#endif
