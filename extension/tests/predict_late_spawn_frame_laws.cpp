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

namespace TestPredictLateSpawnFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *RUNNER_ID = "late_spawned_runner";

constexpr const char *RUNNER_SOURCE = R"(extends Node3D

var beat := 0
var motion := 0
var first_moved := -1
var last_moved := -1


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"motion").input()


func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	beat += motion
	if motion > 0 and is_fresh:
		last_moved = tick
		if first_moved < 0:
			first_moved = tick
)";

constexpr int SERVER = -1;
constexpr int A = 0;
constexpr int TICKRATE = 30;
constexpr int LATE_FRAMES = 20;
constexpr int DRIVEN_FRAMES = 24;

Script *runner_script = nullptr;

Node *build_runner(const Variant &p_name) {
    if (runner_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(runner_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct Moved {
    int64_t slot = -1;
    int64_t beat = -1;
    int64_t first = -1;
    int64_t last = -1;
};

struct Runner {
    Moved server;
    Moved client;
};

struct LateSpawnEvidence {
    bool driven = false;
    int64_t faults = -1;
    Runner early;
    Runner late;
};

LateSpawnEvidence &late_spawn_evidence() {
    static LateSpawnEvidence evidence;
    return evidence;
}

class LateSpawnScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int early = 0;
    int late = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    netw_test::FaultWatch *faults = nullptr;
    Node *host_arena = nullptr;
    Ref<Script> runner;

    Moved read(int p_side, int p_route) const {
        Moved moved;
        Node *node = stand->node_at(p_side, p_route);
        if (node == nullptr) {
            return moved;
        }
        moved.slot = stand->session(p_side)->get_prediction_engine()->slot_of(
            NetwEntity::of(node)
        );
        moved.beat = int64_t(node->get("beat"));
        moved.first = int64_t(node->get("first_moved"));
        moved.last = int64_t(node->get("last_moved"));
        return moved;
    }

    Runner read(int p_route) const {
        Runner read_back;
        read_back.server = read(SERVER, p_route);
        read_back.client = read(A, p_route);
        return read_back;
    }

    int spawn(const char *p_name) {
        NetwMultiplayer *server = stand->session(SERVER);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(A));
        Array args;
        args.push_back(String(p_name));
        const RID made = server->spawn_registered(
            StringName(RUNNER_ID),
            args,
            owner.ptr()
        );
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return 0;
        }
        host_arena->add_child(built);
        return int(server->entity_get_route(made));
    }

    bool open() {
        runner = netw_test::minted_script(RUNNER_SOURCE);
        if (runner.is_null()) {
            return false;
        }
        runner_script = runner.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(RUNNER_ID), callable_mp_static(&build_runner));
        stand->mount();
        host_arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        early = spawn("Early");
        return early > 0;
    }

    void steer(int p_route, int p_motion) {
        if (Node *controlled = stand->node_at(A, p_route)) {
            controlled->set("motion", p_motion);
        }
    }

    void close() {
        late_spawn_evidence().driven = true;
        delete faults;
        faults = nullptr;
        delete stand;
        stand = nullptr;
        runner_script = nullptr;
        runner = Ref<Script>();
    }

    bool finish() {
        close();
        done = true;
        return false;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            return finish();
        }
        if (frame == LATE_FRAMES) {
            faults = new netw_test::FaultWatch();
            late = spawn("Late");
            if (late <= 0) {
                return finish();
            }
        }
        if (late > 0) {
            steer(early, 1);
            steer(late, 1);
        }
        if (frame == LATE_FRAMES + DRIVEN_FRAMES) {
            LateSpawnEvidence &evidence = late_spawn_evidence();
            evidence.faults = faults->errors();
            evidence.early = read(early);
            evidence.late = read(late);
            return finish();
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(LateSpawnScenario, late_spawn_scenario);

TEST_CASE(
    "[Networked][Predict][Frame] a predicted client-controlled entity spawned "
    "after the session started raises no fault"
) {
    const LateSpawnEvidence &evidence = late_spawn_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GE(evidence.late.server.slot, int64_t(0));
    NETW_CHECK_GE(evidence.late.client.slot, int64_t(0));
    NETW_CHECK_EQ(evidence.faults, int64_t(0));
}

TEST_CASE(
    "[Networked][Predict][Frame] the server runs a late-spawned predicted "
    "entity's commands from its controller's first authored tick, as "
    "promptly as an entity spawned at session start"
) {
    const LateSpawnEvidence &evidence = late_spawn_evidence();
    REQUIRE(evidence.driven);
    const Runner &late = evidence.late;
    const Runner &early = evidence.early;
    NETW_CHECK_GT(late.client.first, int64_t(LATE_FRAMES - 1));
    NETW_CHECK_EQ(late.server.first, late.client.first);
    NETW_CHECK_EQ(late.client.last, early.client.last);
    NETW_CHECK_EQ(late.server.last, early.server.last);
    NETW_CHECK_EQ(late.server.beat, late.server.last - late.server.first + 1);
}

} // namespace TestPredictLateSpawnFrameLaws

#endif
