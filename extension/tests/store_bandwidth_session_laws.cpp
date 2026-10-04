#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/stream_seat.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/session_config.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/replication_send.hpp"
#include "netw/wire/registry.hpp"

namespace TestStoreBandwidthSession {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int TICKRATE = 60;
constexpr int BODIES = 240;
constexpr int64_t SESSION_KBPS = 256;
constexpr int64_t SLOW_KBPS = 64;
constexpr int64_t FRAME_SLACK = 64;

int64_t per_tick(int64_t p_kbps) {
    return p_kbps * 125 / TICKRATE;
}

netw::ReplicationSend *row_send_of(NetwMultiplayer *p_core) {
    netw::ReplicationCore *plane = p_core->get_replication_plane();
    return plane != nullptr ? plane->get_sync_pipeline()->row_send_under_test()
                            : nullptr;
}

int64_t rows_to(NetwMultiplayer *p_core, int64_t p_peer) {
    static const int64_t row
        = netw::wire::builtin_channel(StringName("SYNC_ROW"));
    const Dictionary out
        = p_core->attribution_snapshot()[StringName("bytes_out")];
    const Dictionary peer = out.get(p_peer, Dictionary());
    return int64_t(peer.get(row, 0));
}

struct Wake {
    LoopbackRig rig;
    LocalVector<Node2D *> bodies;
    int64_t step = 0;

    explicit Wake(int64_t p_slow_peer_kbps = 0) : rig(2) {
        rig.mount();
        rig.declare_world(WorldDecl().clocked(TICKRATE, 0));
        NetwMultiplayer *server = rig.server();
        const Ref<netw::NetwSessionConfig> config
            = server->session_get_config();
        config->set_bandwidth_kbps(SESSION_KBPS);
        NETW_CHECK_EQ(int(server->session_initialize(config)), int(OK));
        if (p_slow_peer_kbps > 0) {
            server->session_set_peer_bandwidth(
                rig.peer_id(0),
                p_slow_peer_kbps
            );
        }
        for (int at = 0; at < BODIES; ++at) {
            const RID entity = rig.declare_entity(
                EntityDecl()
                    .named(StringName(vformat("Wake%d", at)))
                    .on_schema("WakePose")
                    .synced(StringName("position"))
                    .mounted()
                    .placed_at(Vector2(double(at), 0.0))
            );
            bodies.push_back(Object::cast_to<Node2D>(rig.node_of(entity)));
        }
        move(1);
        netw::ReplicationSend *send = row_send_of(server);
        if (send != nullptr) {
            for (int client = 0; client < 2; ++client) {
                netw_test::seat_pending(*send, int(rig.peer_id(client)));
            }
        }
    }

    void move(int p_ticks) {
        for (int tick = 0; tick < p_ticks; ++tick) {
            step += 1;
            for (uint32_t at = 0; at < bodies.size(); ++at) {
                if (bodies[at] != nullptr) {
                    bodies[at]->set_position(
                        Vector2(double(at), double(step) * 0.5)
                    );
                }
            }
            rig.step_ticks(1);
        }
    }
};

struct Spent {
    int64_t most = 0;
    int64_t total = 0;
    int64_t ticks = 0;
};

Spent watch(Wake &r_wake, int64_t p_peer, int p_ticks) {
    Spent out;
    NetwMultiplayer *server = r_wake.rig.server();
    int64_t before = rows_to(server, p_peer);
    for (int tick = 0; tick < p_ticks; ++tick) {
        r_wake.move(1);
        const int64_t now = rows_to(server, p_peer);
        const int64_t spent = now - before;
        before = now;
        out.most = spent > out.most ? spent : out.most;
        out.total += spent;
        out.ticks += 1;
    }
    return out;
}

TEST_CASE(
    "[Networked][Store][SceneTree] BW1 a session at 256 kbit/s never sends a "
    "peer more than its 533 bytes of rows a tick while hundreds of bodies move"
) {
    Wake wake;
    wake.move(60);
    for (int client = 0; client < 2; ++client) {
        const int64_t peer = wake.rig.peer_id(client);
        const Spent spent = watch(wake, peer, 60);
        const int64_t fill = per_tick(SESSION_KBPS);
        NETW_CHECK_GT(spent.total, fill * spent.ticks / 2);
        NETW_CHECK_LE(spent.most, fill + FRAME_SLACK);
        NETW_CHECK_LE(spent.total, fill * (spent.ticks + 2));
    }
}

TEST_CASE(
    "[Networked][Store][SceneTree] BW2 a peer given its own rate is held to "
    "it while the other peers keep the session's"
) {
    Wake wake(SLOW_KBPS);
    const int64_t slow = wake.rig.peer_id(0);
    const int64_t fast = wake.rig.peer_id(1);
    wake.move(90);
    const Spent held = watch(wake, slow, 60);
    const Spent kept = watch(wake, fast, 60);
    NETW_CHECK_GT(held.total, int64_t(0));
    NETW_CHECK_LE(held.most, per_tick(SLOW_KBPS) + FRAME_SLACK);
    NETW_CHECK_LE(held.total, per_tick(SLOW_KBPS) * (held.ticks + 2));
    NETW_CHECK_GT(kept.total, per_tick(SLOW_KBPS) * (kept.ticks + 2));
    NETW_CHECK_LE(kept.most, per_tick(SESSION_KBPS) + FRAME_SLACK);
}

} // namespace TestStoreBandwidthSession

#endif
