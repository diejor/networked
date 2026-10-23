#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <memory>
#include <vector>

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/offline_multiplayer_peer.hpp>

#include "godot/node.hpp"
#include "godot/performance.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"

using namespace godot;

namespace TestSessionEndReclaimFrameLaws {

using netw::NetwMultiplayer;
using netw::NetwSession;

constexpr int SEATS = 3;
constexpr int HOST = 0;
constexpr int STAYS = 1;
constexpr int LEAVES = 2;
constexpr int CYCLES = 2;
constexpr int WAIT_BUDGET = 240;
constexpr int SETTLE_FRAMES = 4;
constexpr int LINGER_FRAMES = 150;
constexpr const char *BODY_ID = "reclaim_body";

Node *build_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

struct Census {
    int tracked = 0;
    int alive = 0;
};

struct CycleEvidence {
    bool ended = false;
    Census peers[SEATS];
    bool placed_alive[SEATS] = {false, false, false};
    int64_t nodes_after = -1;
};

struct ReclaimEvidence {
    bool driven = false;
    bool seated = false;
    int stalled_at = -1;
    int64_t nodes_before = -1;
    CycleEvidence cycles[CYCLES];
    bool leaver_body_on_host = true;
    bool leaver_body_on_stayer = true;
    Census leaver_after_exit;
    Census bystanders_after_leave;
    bool orphan_alive = true;
    int64_t orphan_warnings = 0;
    bool removed_kept = false;
    Census removed_replicas;
    bool moved_alive = true;
    bool linger_alive = true;
    bool transfer_seated = false;
    bool transfer_exited = true;
    Census transfer_survivors;
    int host_leave_reason = -1;
    int stayer_reason = -1;
};

ReclaimEvidence &evidence() {
    static ReclaimEvidence held;
    return held;
}

Node *node_of(uint64_t p_id) {
    return Object::cast_to<Node>(netw::gd::instance_from_id(ObjectID(p_id)));
}

bool alive(uint64_t p_id) {
    return netw::gd::instance_from_id(ObjectID(p_id)) != nullptr;
}

Census census(const std::vector<uint64_t> &p_ids) {
    Census out;
    for (const uint64_t id : p_ids) {
        out.tracked += 1;
        out.alive += alive(id) ? 1 : 0;
    }
    return out;
}

int64_t node_count() {
    Performance *monitors = Performance::get_singleton();
    return int64_t(monitors->get_monitor(Performance::OBJECT_NODE_COUNT))
        + int64_t(monitors->get_monitor(Performance::OBJECT_ORPHAN_NODE_COUNT));
}

class ReclaimScenario final : public netw_test::FrameScenario {
    int step = 0;
    int waited = 0;
    Ref<netw::LocalLoopbackSession> link;
    Ref<NetwMultiplayer> apis[SEATS];
    Node *mounts[SEATS] = {nullptr, nullptr, nullptr};
    std::unique_ptr<netw_test::CallLog> logs[SEATS];
    std::vector<uint64_t> spawned[SEATS];
    std::vector<int> spawned_routes[SEATS];
    std::vector<int> routes;
    uint64_t placed[SEATS] = {0, 0, 0};
    uint64_t orphan = 0;
    uint64_t removed = 0;
    int removed_route = 0;
    std::vector<uint64_t> removed_replicas;
    uint64_t moved = 0;
    uint64_t linger = 0;
    int host_body = 0;
    int leaver_body = 0;
    netw_test::FaultWatch *faults = nullptr;

    NetwMultiplayer *seat(int p_at) const {
        return apis[p_at].ptr();
    }

    int peer_of(int p_at) const {
        return seat(p_at)->get_unique_id();
    }

    int exits(int p_at) const {
        return logs[p_at]->count(StringName("exited"));
    }

    void pump(int p_rounds) {
        for (int round = 0; round < p_rounds; ++round) {
            if (link.is_valid()) {
                link->poll();
            }
            for (int at = 0; at < SEATS; ++at) {
                if (apis[at].is_valid()) {
                    apis[at]->poll();
                }
            }
        }
    }

    void open_seats() {
        Node *root = netw::gd::scene_root();
        SceneTree *tree = netw::gd::scene_tree();
        for (int at = 0; at < SEATS; ++at) {
            apis[at] = NetwMultiplayer::create(Ref<Script>());
            apis[at]->session_get_inner()->set_root_path(NodePath("/"));
            Node *branch = memnew(Node);
            branch->set_name(vformat("Reclaim%d", at));
            root->add_child(branch);
            mounts[at] = branch;
            apis[at]->session_set_root(
                Callable(branch, "get_node").bind(NodePath("."))
            );
            tree->set_multiplayer(
                Ref<MultiplayerAPI>(
                    Object::cast_to<MultiplayerAPI>(apis[at].ptr())
                ),
                branch->get_path()
            );
            Node2D *hand_placed = memnew(Node2D);
            hand_placed->set_name("Placed");
            branch->add_child(hand_placed);
            placed[at] = uint64_t(hand_placed->get_instance_id());
            Array types;
            types.push_back(int(Variant::STRING));
            Array quantizers;
            quantizers.push_back(Variant());
            apis[at]->spawn_register_constructor(
                StringName(BODY_ID),
                callable_mp_static(&build_body),
                types,
                quantizers
            );
            logs[at] = std::make_unique<netw_test::CallLog>();
            const Ref<NetwSession> door = apis[at]->get_session();
            door->connect(StringName("exited"), logs[at]->callable("exited"));
        }
    }

    void connect_link(bool p_transfers) {
        link.instantiate();
        if (p_transfers) {
            link->enable_server_transfer();
        }
        for (int at = 0; at < SEATS; ++at) {
            spawned[at].clear();
            spawned_routes[at].clear();
            if (p_transfers) {
                apis[at]->transfer_set_mode(NetwSession::TRANSFER_CONTINUE);
            }
            apis[at]->set(
                "multiplayer_peer",
                at == HOST ? link->get_server_peer()
                           : link->create_client_peer()
            );
        }
        routes.clear();
        pump(2);
        apis[HOST]->session_submit_join(StringName("host"), Array());
        pump(4);
        for (int at = 1; at < SEATS; ++at) {
            apis[at]->session_submit_join(
                StringName(vformat("guest%d", at)),
                Array()
            );
        }
        pump(8);
    }

    bool everyone_seated() const {
        for (int at = 0; at < SEATS; ++at) {
            if (seat(HOST)->peer_get_player(peer_of(at)).is_null()) {
                return false;
            }
        }
        return true;
    }

    Node *spawn(const String &p_name, int p_owner, bool p_mount) {
        Ref<netw::NetwPlayer> player;
        if (p_owner >= 0) {
            player = seat(HOST)->peer_get_player(peer_of(p_owner));
        }
        Array args;
        args.push_back(p_name);
        const RID made = seat(HOST)->spawn_registered(
            StringName(BODY_ID),
            args,
            player.ptr()
        );
        Node *node = seat(HOST)->entity_get_node(made);
        if (node == nullptr) {
            return nullptr;
        }
        int route = 0;
        if (p_mount) {
            mounts[HOST]->add_child(node);
            route = int(seat(HOST)->entity_get_route(made));
            routes.push_back(route);
        }
        spawned[HOST].push_back(uint64_t(node->get_instance_id()));
        spawned_routes[HOST].push_back(route);
        return node;
    }

    void track_replicas() {
        pump(8);
        for (int at = 1; at < SEATS; ++at) {
            for (const int route : routes) {
                Node *node = seat(at)->entity_get_node(
                    seat(at)->entity_from_route(route)
                );
                if (node != nullptr) {
                    spawned[at].push_back(uint64_t(node->get_instance_id()));
                    spawned_routes[at].push_back(route);
                }
            }
        }
    }

    void populate_bodies() {
        for (int at = 0; at < SEATS; ++at) {
            if (spawn(vformat("Body%d", at), at, true) == nullptr) {
                continue;
            }
            if (at == HOST) {
                host_body = routes[routes.size() - 1];
            }
            if (at == LEAVES) {
                leaver_body = routes[routes.size() - 1];
            }
        }
        spawn("Loose", -1, true);
    }

    void populate_edges() {
        const Node *never_added = spawn("Orphan", -1, false);
        orphan = never_added != nullptr
            ? uint64_t(never_added->get_instance_id())
            : 0;
        const Node *pulled = spawn("Removed", -1, true);
        removed = pulled != nullptr ? uint64_t(pulled->get_instance_id()) : 0;
        removed_route = routes.empty() ? 0 : routes[routes.size() - 1];
        const Node *carried = spawn("Moved", -1, true);
        moved = carried != nullptr ? uint64_t(carried->get_instance_id()) : 0;
        const Node *lingering = spawn("Linger", -1, true);
        linger = lingering != nullptr
            ? uint64_t(lingering->get_instance_id())
            : 0;
        seat(HOST)->replication_adopt_in_place(
            mounts[HOST]->get_node_or_null(NodePath("Placed"))
        );
    }

    void move_edges() {
        Node *pulled = node_of(removed);
        if (pulled != nullptr && pulled->get_parent() != nullptr) {
            pulled->get_parent()->remove_child(pulled);
        }
        for (int at = 0; at < SEATS; ++at) {
            for (size_t row = spawned[at].size(); row-- > 0;) {
                if (spawned_routes[at][row] != removed_route) {
                    continue;
                }
                if (at != HOST) {
                    removed_replicas.push_back(spawned[at][row]);
                }
                spawned[at].erase(spawned[at].begin() + row);
                spawned_routes[at].erase(spawned_routes[at].begin() + row);
            }
        }
        Node *carried = node_of(moved);
        if (carried != nullptr) {
            carried->reparent(netw::gd::scene_root());
        }
        pump(4);
    }

    void start_linger() {
        Node *lingering = node_of(linger);
        if (lingering == nullptr) {
            return;
        }
        Ref<netw::NetwDespawnOpts> opts;
        opts.instantiate();
        opts->set_linger(true);
        opts->set_linger_seconds(1.0);
        seat(HOST)->entity_despawn(seat(HOST)->entity_of(lingering), opts);
        pump(1);
    }

    bool all_exited(int p_cycle_exits) const {
        for (int at = 0; at < SEATS; ++at) {
            if (exits(at) < p_cycle_exits) {
                return false;
            }
        }
        return true;
    }

    void record_cycle(int p_cycle) {
        CycleEvidence &seen = evidence().cycles[p_cycle];
        seen.ended = true;
        for (int at = 0; at < SEATS; ++at) {
            seen.peers[at] = census(spawned[at]);
            seen.placed_alive[at] = alive(placed[at]);
        }
        seen.nodes_after = node_count();
    }

    void record_leave() {
        ReclaimEvidence &seen = evidence();
        seen.leaver_after_exit = census(spawned[LEAVES]);
        seen.removed_replicas = census(removed_replicas);
        std::vector<uint64_t> bystanders;
        for (int at = 0; at < LEAVES; ++at) {
            for (size_t row = 0; row < spawned[at].size(); ++row) {
                const int route = spawned_routes[at][row];
                const uint64_t id = spawned[at][row];
                if (route == leaver_body && at == HOST) {
                    seen.leaver_body_on_host = alive(id);
                } else if (route == leaver_body) {
                    seen.leaver_body_on_stayer = alive(id);
                } else {
                    bystanders.push_back(id);
                }
            }
        }
        seen.bystanders_after_leave = census(bystanders);
    }

    bool wait_until(bool p_ready) {
        if (p_ready) {
            waited = 0;
            return true;
        }
        waited += 1;
        if (waited > WAIT_BUDGET) {
            evidence().stalled_at = step;
            step = 99;
        }
        return false;
    }

    void close_seats() {
        for (int at = 0; at < SEATS; ++at) {
            if (apis[at].is_valid()) {
                apis[at]->set("multiplayer_peer", Ref<MultiplayerPeer>());
            }
        }
        SceneTree *tree = netw::gd::scene_tree();
        for (int at = 0; at < SEATS; ++at) {
            if (mounts[at] == nullptr) {
                continue;
            }
            tree->set_multiplayer(
                Ref<MultiplayerAPI>(),
                mounts[at]->get_path()
            );
            mounts[at]->get_parent()->remove_child(mounts[at]);
            memdelete(mounts[at]);
            mounts[at] = nullptr;
            logs[at].reset();
            apis[at].unref();
        }
        Node *carried = node_of(moved);
        if (carried != nullptr) {
            carried->get_parent()->remove_child(carried);
            memdelete(carried);
        }
        if (link.is_valid()) {
            link->reset();
            link.unref();
        }
    }

public:
    bool advance() override {
        ReclaimEvidence &seen = evidence();
        if (seen.driven || netw::gd::scene_root() == nullptr) {
            return false;
        }
        pump(2);
        switch (step) {
            case 0: {
                faults = new netw_test::FaultWatch;
                open_seats();
                seen.nodes_before = node_count();
                connect_link(false);
                step = 1;
                return true;
            }
            case 1: {
                if (!wait_until(everyone_seated())) {
                    pump(4);
                    return true;
                }
                seen.seated = true;
                populate_bodies();
                populate_edges();
                track_replicas();
                step = 2;
                return true;
            }
            case 2: {
                move_edges();
                seat(LEAVES)->session_leave();
                step = 3;
                return true;
            }
            case 3: {
                if (!wait_until(exits(LEAVES) >= 1)) {
                    return true;
                }
                step = 4;
                return true;
            }
            case 4: {
                if (++waited < SETTLE_FRAMES) {
                    return true;
                }
                waited = 0;
                record_leave();
                start_linger();
                seat(HOST)->session_leave();
                step = 5;
                return true;
            }
            case 5: {
                if (!wait_until(all_exited(1))) {
                    return true;
                }
                seen.host_leave_reason
                    = int(logs[HOST]->args(StringName("exited"), 0)[0]);
                seen.stayer_reason
                    = int(logs[STAYS]->args(StringName("exited"), 0)[0]);
                seat(HOST)->set(
                    "multiplayer_peer",
                    Ref<MultiplayerPeer>(memnew(OfflineMultiplayerPeer))
                );
                step = 6;
                return true;
            }
            case 6: {
                if (++waited < SETTLE_FRAMES) {
                    return true;
                }
                waited = 0;
                seen.orphan_alive = alive(orphan);
                seen.moved_alive = alive(moved);
                Node *kept = node_of(removed);
                seen.removed_kept = kept != nullptr;
                if (kept != nullptr) {
                    memdelete(kept);
                }
                record_cycle(0);
                step = 7;
                return true;
            }
            case 7: {
                if (++waited < LINGER_FRAMES && alive(linger)) {
                    return true;
                }
                waited = 0;
                seen.linger_alive = alive(linger);
                connect_link(false);
                step = 8;
                return true;
            }
            case 8: {
                if (!wait_until(everyone_seated())) {
                    pump(4);
                    return true;
                }
                populate_bodies();
                track_replicas();
                const Ref<NetwSession> door = seat(HOST)->get_session();
                door->kick(
                    door->player_of(peer_of(LEAVES)),
                    String("reclaim")
                );
                step = 9;
                return true;
            }
            case 9: {
                if (!wait_until(exits(LEAVES) >= 2)) {
                    return true;
                }
                seat(HOST)->set("multiplayer_peer", Ref<MultiplayerPeer>());
                link->get_server_peer()->close();
                step = 10;
                return true;
            }
            case 10: {
                if (!wait_until(all_exited(2))) {
                    return true;
                }
                step = 11;
                return true;
            }
            case 11: {
                if (++waited < SETTLE_FRAMES) {
                    return true;
                }
                waited = 0;
                record_cycle(1);
                connect_link(true);
                step = 12;
                return true;
            }
            case 12: {
                if (!wait_until(everyone_seated())) {
                    pump(4);
                    return true;
                }
                seen.transfer_seated = true;
                populate_bodies();
                track_replicas();
                link->lose_server();
                step = 13;
                return true;
            }
            case 13: {
                if (++waited < SETTLE_FRAMES * 4) {
                    return true;
                }
                waited = 0;
                std::vector<uint64_t> survivors;
                for (int at = 1; at < SEATS; ++at) {
                    for (size_t row = 0; row < spawned[at].size(); ++row) {
                        if (spawned_routes[at][row] != host_body) {
                            survivors.push_back(spawned[at][row]);
                        }
                    }
                }
                seen.transfer_survivors = census(survivors);
                seen.transfer_exited = exits(STAYS) > 2 || exits(LEAVES) > 2;
                step = 99;
                return true;
            }
            default: {
                close_seats();
                seen.orphan_warnings = faults->warnings_containing(
                    "never entered the tree"
                );
                delete faults;
                faults = nullptr;
                seen.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(ReclaimScenario, session_end_reclaim_scenario);

const ReclaimEvidence &driven() {
    const ReclaimEvidence &seen = evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    NETW_FORMAT_INT(stalled_text, seen.stalled_at);
    CAPTURE(stalled_text);
    REQUIRE(seen.stalled_at < 0);
    return seen;
}

} // namespace TestSessionEndReclaimFrameLaws

using TestSessionEndReclaimFrameLaws::CYCLES;
using TestSessionEndReclaimFrameLaws::CycleEvidence;
using TestSessionEndReclaimFrameLaws::driven;
using TestSessionEndReclaimFrameLaws::NetwSession;
using TestSessionEndReclaimFrameLaws::ReclaimEvidence;
using TestSessionEndReclaimFrameLaws::SEATS;

TEST_CASE(
    "[Networked][Session][Frame] SR1 every node the session spawned is freed "
    "on the server and on every client once the session ends, in each of two "
    "host cycles"
) {
    const ReclaimEvidence &seen = driven();
    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        const CycleEvidence &ended = seen.cycles[cycle];
        REQUIRE(ended.ended);
        for (int at = 0; at < SEATS; ++at) {
            NETW_FORMAT_INT(cycle_text, cycle);
            NETW_FORMAT_INT(seat_text, at);
            CAPTURE(cycle_text);
            CAPTURE(seat_text);
            NETW_CHECK_GT(ended.peers[at].tracked, 0);
            NETW_CHECK_EQ(ended.peers[at].alive, 0);
        }
    }
}

TEST_CASE(
    "[Networked][Session][Frame] SR2 an entity the game placed by hand "
    "survives the session ending on every peer"
) {
    const ReclaimEvidence &seen = driven();
    for (int cycle = 0; cycle < CYCLES; ++cycle) {
        for (int at = 0; at < SEATS; ++at) {
            NETW_FORMAT_INT(cycle_text, cycle);
            NETW_FORMAT_INT(seat_text, at);
            CAPTURE(cycle_text);
            CAPTURE(seat_text);
            CHECK(seen.cycles[cycle].placed_alive[at]);
        }
    }
}

TEST_CASE(
    "[Networked][Session][Frame] SR3 hosting again leaves no more nodes "
    "behind than the first session did"
) {
    const ReclaimEvidence &seen = driven();
    NETW_CHECK_EQ(seen.cycles[0].nodes_after, seen.nodes_before);
    NETW_CHECK_EQ(seen.cycles[1].nodes_after, seen.nodes_before);
}

TEST_CASE(
    "[Networked][Session][Frame] SR4 a client that leaves frees its own "
    "replicas, its player's node is despawned on the host and on the other "
    "client, and every other node stays"
) {
    const ReclaimEvidence &seen = driven();
    CHECK_FALSE(seen.leaver_body_on_host);
    CHECK_FALSE(seen.leaver_body_on_stayer);
    NETW_CHECK_GT(seen.leaver_after_exit.tracked, 0);
    NETW_CHECK_EQ(seen.leaver_after_exit.alive, 0);
    NETW_CHECK_GT(seen.bystanders_after_leave.tracked, 0);
    NETW_CHECK_EQ(
        seen.bystanders_after_leave.alive,
        seen.bystanders_after_leave.tracked
    );
}

TEST_CASE(
    "[Networked][Session][Frame] SR5 a spawned node that never entered the "
    "tree and one moved outside the session root are freed when the session "
    "ends"
) {
    const ReclaimEvidence &seen = driven();
    CHECK_FALSE(seen.orphan_alive);
    CHECK_FALSE(seen.moved_alive);
}

TEST_CASE(
    "[Networked][Session][Frame] SR9 a spawned node that never entered the "
    "tree is reported once, though the session then ends"
) {
    NETW_CHECK_EQ(driven().orphan_warnings, 1);
}

TEST_CASE(
    "[Networked][Session][Frame] SR8 a spawned node the game takes out of the "
    "tree is despawned on the clients and stays the game's to free"
) {
    const ReclaimEvidence &seen = driven();
    CHECK(seen.removed_kept);
    NETW_CHECK_GT(seen.removed_replicas.tracked, 0);
    NETW_CHECK_EQ(seen.removed_replicas.alive, 0);
}

TEST_CASE(
    "[Networked][Session][Frame] SR6 a node still lingering after its despawn "
    "when the session ends is freed all the same"
) {
    const ReclaimEvidence &seen = driven();
    CHECK_FALSE(seen.linger_alive);
}

TEST_CASE(
    "[Networked][Session][Frame] SR7 a server lost under TRANSFER_CONTINUE "
    "ends no client session and frees nothing the clients hold"
) {
    const ReclaimEvidence &seen = driven();
    REQUIRE(seen.transfer_seated);
    CHECK_FALSE(seen.transfer_exited);
    NETW_CHECK_GT(seen.transfer_survivors.tracked, 0);
    NETW_CHECK_EQ(
        seen.transfer_survivors.alive,
        seen.transfer_survivors.tracked
    );
}

TEST_CASE(
    "[Networked][Session][Frame] SR10 a host that leaves exits with "
    "LOCAL_LEAVE and the client still seated exits with SERVER_SHUTDOWN"
) {
    const ReclaimEvidence &seen = driven();
    NETW_CHECK_EQ(seen.host_leave_reason, int(NetwSession::LOCAL_LEAVE));
    NETW_CHECK_EQ(seen.stayer_reason, int(NetwSession::SERVER_SHUTDOWN));
}

#endif
