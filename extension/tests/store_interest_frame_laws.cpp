#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestNetwStoreInterest {

using netw::NetwMultiplayer;
using netw::store::RowAddress;
using netw::store::Stream;
using netw::store::Table;
using netw::store::World;

constexpr const char *MARK_ID = "store_interest_mark";

constexpr const char *MARK_SOURCE = R"(extends Node3D

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"position").broadcast()
)";

constexpr int TICKRATE = 30;
constexpr int SETTLE_FRAMES = 20;
constexpr int READ_FRAMES = 2;
constexpr int CROWD = 900;
constexpr int JOIN_FRAMES = 10;
constexpr int STEADY_FRAMES = 5;

Script *mark_script = nullptr;

Node *build_mark(const Variant &p_name) {
    if (mark_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(mark_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

RowAddress row_of_node(const World &p_world, Node *p_node) {
    RowAddress found;
    const uint64_t id = uint64_t(p_node->get_instance_id());
    for (uint32_t table = 0; table < p_world.table_count(); ++table) {
        const Table &held = p_world.table(table);
        for (uint32_t row = 0; row < held.row_count(); ++row) {
            if (held.nodes[row] == id) {
                found.table = table;
                found.row = row;
            }
        }
    }
    return found;
}

struct Seen {
    bool admitted = false;
    bool visible = false;
    bool held = false;
    bool live = false;
    int stream = -1;
};

class MarkStand {
protected:
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    LocalVector<RID> marks;
    LocalVector<RID> layers;

    RID layer() const {
        return layers[0];
    }

    NetwMultiplayer *server() const {
        return stand->session(-1);
    }

    int client_id() const {
        return stand->peer_id(0);
    }

    Node *mark(int p_index) const {
        return server()->entity_get_node(marks[p_index]);
    }

    int64_t route_of(int p_index) const {
        return server()->get_liveness_core()->route_of(marks[p_index]);
    }

    Seen seen_by_client(int p_index) const {
        Seen out;
        const World &world = server()->store();
        const RowAddress at = row_of_node(world, mark(p_index));
        const uint32_t peer = world.peer_of(client_id());
        if (!at.valid() || peer == Table::NO_SLOT) {
            return out;
        }
        const Table &table = world.table(at.table);
        const uint32_t slot = table.slots[at.row];
        out.admitted = table.peers[peer].admitted.get(at.row);
        out.visible = world.reaches(slot, peer);
        out.held = world.holds_copy(slot, peer);
        out.live = world.is_live(slot);
        out.stream = int(table.peers[peer].stream[at.row]);
        return out;
    }

    int admitted_to_client() const {
        const World &world = server()->store();
        const uint32_t peer = world.peer_of(client_id());
        if (peer == Table::NO_SLOT) {
            return 0;
        }
        int total = 0;
        for (uint32_t table = 0; table < world.table_count(); ++table) {
            total += int(world.table(table).peers[peer].admitted.population());
        }
        return total;
    }

    bool open_stand(int p_marks) {
        script = netw_test::minted_script(MARK_SOURCE);
        if (script.is_null()) {
            return false;
        }
        mark_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(MARK_ID), callable_mp_static(&build_mark));
        stand->mount();
        stand->arm(TICKRATE);
        Node *arena = stand->arena();
        for (int index = 0; index < p_marks; ++index) {
            Array args;
            args.push_back(String("mark") + String::num_int64(index));
            const RID made = server()->spawn_registered(
                StringName(MARK_ID),
                args,
                nullptr
            );
            Node *built = server()->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
            marks.push_back(made);
        }
        return true;
    }

    void close_stand() {
        delete stand;
        stand = nullptr;
        mark_script = nullptr;
        script = Ref<Script>();
        marks.clear();
        layers.clear();
    }
};

struct AdmitEvidence {
    bool driven = false;
    Seen hidden_before;
    Seen hidden_after;
    Seen unheld_before;
    Seen unheld_after;
    Seen dead_before;
    Seen dead_after;
};

AdmitEvidence &admit_evidence() {
    static AdmitEvidence evidence;
    return evidence;
}

class AdmitScenario final : public netw_test::FrameScenario, MarkStand {
    int frame = 0;
    bool done = false;
    bool open() {
        if (!open_stand(3)) {
            return false;
        }
        layers.push_back(server()->interest_layer_create(StringName("sight")));
        server()->interest_layer_set_param(
            layer(),
            NetwMultiplayer::LAYER_PARAM_LEAVE_POLICY,
            int(NetwMultiplayer::LEAVE_POLICY_RETAIN)
        );
        server()->interest_layer_add_viewer(layer(), client_id());
        server()->interest_layer_add_entity(layer(), marks[0]);
        stand->pump(8);
        return true;
    }

    void act() {
        AdmitEvidence &seen = admit_evidence();
        seen.hidden_before = seen_by_client(0);
        seen.unheld_before = seen_by_client(1);
        seen.dead_before = seen_by_client(2);
        server()->interest_layer_remove_viewer(layer(), client_id());
        server()->lifecycle_withdraw_author(route_of(1), client_id());
        server()->get_liveness_core()->hide_route(int(route_of(2)));
    }

    void read_withdrawn() {
        AdmitEvidence &seen = admit_evidence();
        seen.unheld_after = seen_by_client(1);
        seen.dead_after = seen_by_client(2);
    }

    void read_hidden() {
        admit_evidence().hidden_after = seen_by_client(0);
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close_stand();
            admit_evidence().driven = true;
            done = true;
            return false;
        }
        if (frame == SETTLE_FRAMES) {
            act();
        }
        if (frame == SETTLE_FRAMES + 1) {
            read_withdrawn();
        }
        if (frame == SETTLE_FRAMES + READ_FRAMES) {
            read_hidden();
            close_stand();
            admit_evidence().driven = true;
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(AdmitScenario, store_admit_scenario);

struct CrowdEvidence {
    bool driven = false;
    int admitted_before = -1;
    int admitted_after = -1;
    int64_t join_evaluations = 0;
    int64_t join_writes = 0;
    int64_t join_marks = 0;
    int64_t steady_evaluations = 0;
    int64_t steady_writes = 0;
};

CrowdEvidence &crowd_evidence() {
    static CrowdEvidence evidence;
    return evidence;
}

class CrowdScenario final : public netw_test::FrameScenario, MarkStand {
    int frame = 0;
    bool done = false;
    World::InterestWork opened;
    int64_t marks_opened = 0;

    bool open() {
        if (!open_stand(CROWD)) {
            return false;
        }
        layers.push_back(server()->interest_layer_create(StringName("crowd")));
        for (int index = 0; index < CROWD; ++index) {
            server()->interest_layer_add_entity(layer(), marks[index]);
        }
        stand->pump(8);
        return true;
    }

    void snapshot() {
        opened = server()->store().interest_work;
        marks_opened = server()->interest_plane().mark_visits;
    }

    void join() {
        CrowdEvidence &seen = crowd_evidence();
        seen.admitted_before = admitted_to_client();
        snapshot();
        server()->interest_layer_add_viewer(layer(), client_id());
    }

    void joined() {
        CrowdEvidence &seen = crowd_evidence();
        const World::InterestWork &now = server()->store().interest_work;
        seen.join_evaluations = now.evaluations - opened.evaluations;
        seen.join_writes = now.writes - opened.writes;
        seen.join_marks = server()->interest_plane().mark_visits - marks_opened;
        seen.admitted_after = admitted_to_client();
        snapshot();
    }

    void settled() {
        CrowdEvidence &seen = crowd_evidence();
        const World::InterestWork &now = server()->store().interest_work;
        seen.steady_evaluations = now.evaluations - opened.evaluations;
        seen.steady_writes = now.writes - opened.writes;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close_stand();
            crowd_evidence().driven = true;
            done = true;
            return false;
        }
        if (frame == SETTLE_FRAMES) {
            join();
        }
        if (frame == SETTLE_FRAMES + JOIN_FRAMES) {
            joined();
        }
        if (frame == SETTLE_FRAMES + JOIN_FRAMES + STEADY_FRAMES) {
            settled();
            close_stand();
            crowd_evidence().driven = true;
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(CrowdScenario, store_crowd_scenario);

TEST_CASE(
    "[Networked][Store][Frame] SI1 an interest hide that leaves the peer "
    "its copy withdraws the row from that peer within a tick and closes its "
    "stream, while the copy stays held"
) {
    const AdmitEvidence &seen = admit_evidence();
    REQUIRE(seen.driven);
    CHECK(seen.hidden_before.admitted);
    CHECK(seen.hidden_before.held);
    NETW_CHECK_EQ(seen.hidden_before.stream, int(Stream::READY));
    CHECK_FALSE(seen.hidden_after.visible);
    CHECK_FALSE(seen.hidden_after.admitted);
    CHECK(seen.hidden_after.held);
    NETW_CHECK_EQ(seen.hidden_after.stream, int(Stream::NONE));
}

TEST_CASE(
    "[Networked][Store][Frame] SI2 a peer that holds no spawned copy is not "
    "admitted to the row, however interest reads"
) {
    const AdmitEvidence &seen = admit_evidence();
    REQUIRE(seen.driven);
    CHECK(seen.unheld_before.admitted);
    CHECK(seen.unheld_after.visible);
    CHECK_FALSE(seen.unheld_after.held);
    CHECK_FALSE(seen.unheld_after.admitted);
}

TEST_CASE(
    "[Networked][Store][Frame] SI3 a row whose entity stops being live is "
    "admitted to nobody"
) {
    const AdmitEvidence &seen = admit_evidence();
    REQUIRE(seen.driven);
    CHECK(seen.dead_before.admitted);
    CHECK(seen.dead_before.live);
    CHECK_FALSE(seen.dead_after.live);
    CHECK_FALSE(seen.dead_after.admitted);
}

TEST_CASE(
    "[Networked][Store][Frame] SI4 a viewer joining a 900 member layer costs "
    "work in proportion to the members, and a steady tick after it costs none"
) {
    const CrowdEvidence &seen = crowd_evidence();
    REQUIRE(seen.driven);
    const bool none_before = seen.admitted_before == 0;
    const bool all_after = seen.admitted_after == CROWD;
    const bool evaluated_each_member
        = seen.join_evaluations >= CROWD && seen.join_evaluations <= 3 * CROWD;
    const bool wrote_each_member
        = seen.join_writes >= CROWD && seen.join_writes <= 3 * CROWD;
    const bool marked_each_member = seen.join_marks <= 3 * CROWD;
    const bool steady_is_free
        = seen.steady_evaluations == 0 && seen.steady_writes == 0;
    CHECK(none_before);
    CHECK(all_after);
    CHECK(evaluated_each_member);
    CHECK(wrote_each_member);
    CHECK(marked_each_member);
    CHECK(steady_is_free);
}

} // namespace TestNetwStoreInterest

#endif
