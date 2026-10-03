#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestEntityPredictedReadmissionFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *ARCHETYPE_ID = "readmitted_archetype_runner";
constexpr const char *DECLARED_ID = "readmitted_declared_runner";

constexpr const char *ARCHETYPE_SOURCE = R"(extends Node3D

var beat := 0
var motion := 0


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"motion").input()


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	beat += 1
)";

constexpr const char *DECLARED_SOURCE = R"(extends Node3D

var beat := 0
var motion := 0


func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"motion").input()


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	beat += 1
)";

constexpr int SERVER = -1;
constexpr int A = 0;
constexpr int TICKRATE = 30;
constexpr int WARM_FRAMES = 12;
constexpr int SETTLE_FRAMES = 8;
constexpr int READMIT_FRAMES = 12;
constexpr int64_t LIVE = int64_t(netw::entity::Stage::LIVE);

Script *archetype_script = nullptr;
Script *declared_script = nullptr;

Node *build_from(Script *p_script, const Variant &p_name) {
    if (p_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(p_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_archetype(const Variant &p_name) {
    return build_from(archetype_script, p_name);
}

Node *build_declared(const Variant &p_name) {
    return build_from(declared_script, p_name);
}

struct Readmitted {
    int64_t seated = -1;
    int64_t released = 0;
    int64_t stage = -1;
    int64_t slot = -1;
    int64_t mirror_slot = -1;
    int64_t beat_before = -1;
    int64_t beat_after = -1;
};

struct ReadmissionEvidence {
    bool driven = false;
    bool seated = false;
    int64_t faults = -1;
    int64_t server_readd_faults = -1;
    Readmitted archetype_server;
    Readmitted archetype_client;
    Readmitted declared_server;
    Readmitted declared_client;
};

ReadmissionEvidence &readmission_evidence() {
    static ReadmissionEvidence evidence;
    return evidence;
}

struct Arm {
    int side = SERVER;
    int route = 0;
    Node *node = nullptr;
    Node *home = nullptr;
    Readmitted *row = nullptr;
};

enum ArmIndex {
    ARCHETYPE_CLIENT,
    DECLARED_SERVER,
    DECLARED_CLIENT,
    ARCHETYPE_SERVER,
    ARM_COUNT,
};

class ReadmissionScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    netw_test::FaultWatch *faults = nullptr;
    Ref<Script> archetype;
    Ref<Script> declared;
    Node *host_arena = nullptr;
    Arm arms[ARM_COUNT];

    int64_t slot_of(int p_side, Node *p_node) const {
        if (p_node == nullptr) {
            return -1;
        }
        return stand->session(p_side)->get_prediction_engine()->slot_of(
            NetwEntity::of(p_node)
        );
    }

    int spawn(const char *p_id, const char *p_name) {
        NetwMultiplayer *server = stand->session(SERVER);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(A));
        Array args;
        args.push_back(String(p_name));
        const RID made
            = server->spawn_registered(StringName(p_id), args, owner.ptr());
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return 0;
        }
        host_arena->add_child(built);
        return int(server->entity_get_route(made));
    }

    void declare_on_every_peer(int p_route) {
        for (int side = SERVER; side <= A; ++side) {
            NetwMultiplayer *api = stand->session(side);
            api->predict_declare(api->entity_from_route(p_route));
        }
    }

    bool seat(ArmIndex p_arm, int p_side, int p_route, Readmitted &r_row) {
        Arm &arm = arms[p_arm];
        arm.side = p_side;
        arm.route = p_route;
        arm.node = stand->node_at(p_side, p_route);
        arm.row = &r_row;
        return arm.node != nullptr;
    }

    bool open() {
        archetype = netw_test::minted_script(ARCHETYPE_SOURCE);
        declared = netw_test::minted_script(DECLARED_SOURCE);
        if (archetype.is_null() || declared.is_null()) {
            return false;
        }
        archetype_script = archetype.ptr();
        declared_script = declared.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(
            StringName(ARCHETYPE_ID),
            callable_mp_static(&build_archetype)
        );
        stand->teach(
            StringName(DECLARED_ID),
            callable_mp_static(&build_declared)
        );
        stand->mount();
        host_arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        const int archetype_client = spawn(ARCHETYPE_ID, "Bob");
        const int declared_server = spawn(DECLARED_ID, "Dee");
        const int declared_client = spawn(DECLARED_ID, "Eve");
        const int archetype_server = spawn(ARCHETYPE_ID, "Ann");
        stand->pump(8);
        declare_on_every_peer(declared_server);
        declare_on_every_peer(declared_client);
        stand->pump(2);
        ReadmissionEvidence &evidence = readmission_evidence();
        return seat(ARCHETYPE_CLIENT,
                    A,
                    archetype_client,
                    evidence.archetype_client)
            && seat(DECLARED_SERVER,
                    SERVER,
                    declared_server,
                    evidence.declared_server)
            && seat(DECLARED_CLIENT,
                    A,
                    declared_client,
                    evidence.declared_client)
            && seat(ARCHETYPE_SERVER,
                    SERVER,
                    archetype_server,
                    evidence.archetype_server);
    }

    void depart() {
        readmission_evidence().seated = true;
        for (Arm &arm : arms) {
            arm.row->seated = slot_of(arm.side, arm.node);
            arm.row->beat_before = int64_t(arm.node->get("beat"));
            arm.home = arm.node->get_parent();
            arm.home->remove_child(arm.node);
        }
    }

    void readd(int p_from, int p_to) {
        for (int at = p_from; at < p_to; ++at) {
            Arm &arm = arms[at];
            arm.row->released = slot_of(arm.side, arm.node);
            arm.home->add_child(arm.node);
        }
    }

    void read(int p_from, int p_to) {
        for (int at = p_from; at < p_to; ++at) {
            const Arm &arm = arms[at];
            const int other = arm.side == SERVER ? A : SERVER;
            const Ref<NetwEntity> entity = NetwEntity::of(arm.node);
            const int route = int(entity->get_route());
            arm.row->stage = entity->get_stage();
            arm.row->slot = slot_of(arm.side, arm.node);
            arm.row->mirror_slot
                = route > 0 ? slot_of(other, stand->node_at(other, route)) : -1;
            arm.row->beat_after = int64_t(arm.node->get("beat"));
        }
    }

    void close() {
        readmission_evidence().driven = true;
        delete faults;
        faults = nullptr;
        delete stand;
        stand = nullptr;
        archetype_script = nullptr;
        declared_script = nullptr;
        archetype = Ref<Script>();
        declared = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        const int readded = WARM_FRAMES + SETTLE_FRAMES;
        const int late = readded + READMIT_FRAMES;
        if (frame == WARM_FRAMES) {
            depart();
        }
        if (frame == readded) {
            faults = new netw_test::FaultWatch();
            readd(ARCHETYPE_CLIENT, ARCHETYPE_SERVER);
        }
        if (frame == late) {
            read(ARCHETYPE_CLIENT, ARCHETYPE_SERVER);
            readmission_evidence().faults = faults->errors();
            readd(ARCHETYPE_SERVER, ARM_COUNT);
        }
        if (frame == late + READMIT_FRAMES) {
            read(ARCHETYPE_SERVER, ARM_COUNT);
            ReadmissionEvidence &evidence = readmission_evidence();
            evidence.server_readd_faults = faults->errors() - evidence.faults;
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ReadmissionScenario, predicted_readmission_scenario);

void check_predicts_again(const Readmitted &p_row) {
    NETW_CHECK_GE(p_row.seated, int64_t(0));
    NETW_CHECK_EQ(p_row.released, int64_t(-1));
    NETW_CHECK_EQ(p_row.stage, LIVE);
    NETW_CHECK_GE(p_row.slot, int64_t(0));
    NETW_CHECK_GT(p_row.beat_after, p_row.beat_before);
}

TEST_CASE(
    "[Networked][Entity][Predict][Frame] a predicted node the server re-adds "
    "after its departure settled predicts again, and so does its new copy"
) {
    const ReadmissionEvidence &evidence = readmission_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    check_predicts_again(evidence.archetype_server);
    NETW_CHECK_GE(evidence.archetype_server.mirror_slot, int64_t(0));
    NETW_CHECK_EQ(evidence.server_readd_faults, int64_t(0));
}

TEST_CASE(
    "[Networked][Entity][Predict][Frame] a predicted copy a client re-adds "
    "after its departure settled predicts again"
) {
    const ReadmissionEvidence &evidence = readmission_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    check_predicts_again(evidence.archetype_client);
    NETW_CHECK_GE(evidence.archetype_client.mirror_slot, int64_t(0));
    NETW_CHECK_EQ(evidence.faults, int64_t(0));
}

TEST_CASE(
    "[Networked][Entity][Predict][Frame] a node declared predicted through "
    "predict_declare predicts again when it is re-added after its departure "
    "settled, on the server and on a client"
) {
    const ReadmissionEvidence &evidence = readmission_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    check_predicts_again(evidence.declared_server);
    check_predicts_again(evidence.declared_client);
    NETW_CHECK_GE(evidence.declared_client.mirror_slot, int64_t(0));
    NETW_CHECK_EQ(evidence.faults, int64_t(0));
}

} // namespace TestEntityPredictedReadmissionFrameLaws

#endif
