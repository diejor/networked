#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestPredictIndependentFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *RUNNER_ID = "independent_runner";

constexpr const char *RUNNER_SOURCE = R"(extends CharacterBody3D

var motion := 0
var replays := 0
var replays_in_physics := 0

func _init() -> void:
	var hull := CollisionShape3D.new()
	hull.shape = BoxShape3D.new()
	add_child(hull)
	collision_layer = 0
	collision_mask = 0
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	e.prediction.recovery_policy = NetwPrediction.RECOVERY_POLICY_REBASE_REPLAY
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"motion").input()

var queries_refused := 0
var motions_refused := 0

func _network_tick(_delta: float, _tick: int, is_fresh: bool) -> void:
	if not is_fresh:
		replays += 1
		if Engine.is_in_physics_frame():
			replays_in_physics += 1
	if get_world_3d().direct_space_state == null:
		queries_refused += 1
	var before := position
	velocity = Vector3(3.0, 0.0, 0.0)
	move_and_slide()
	if position == before:
		motions_refused += 1
)";

constexpr int SERVER = -1;
constexpr int A = 0;
constexpr int TICKRATE = 60;
constexpr int WARM_FRAMES = 20;
constexpr int SETTLE_FRAMES = 30;
constexpr double BUMP = 50.0;

Script *runner_script = nullptr;
netw_test::SimStand *idle_stand = nullptr;

Node *build_runner(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(runner_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

void idle_poll() {
    if (idle_stand == nullptr) {
        return;
    }
    netw::LocalMultiplayerPeer *client = idle_stand->peer(A);
    idle_stand->loopback()->release_inbound_packets(client);
    idle_stand->session(A)->poll();
    idle_stand->loopback()->hold_inbound_packets(client);
}

struct IndependentEvidence {
    bool driven = false;
    bool seated = false;
    int64_t replays = 0;
    int64_t replays_in_physics = 0;
    int64_t queries_refused = 0;
    int64_t motions_refused = 0;
    int64_t faults = 0;
    double server_x = 0.0;
    double client_x = 0.0;
};

IndependentEvidence &independent_evidence() {
    static IndependentEvidence evidence;
    return evidence;
}

class IndependentScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    int route = 0;
    netw_test::SimStand *stand = nullptr;
    netw_test::FaultWatch *faults = nullptr;
    Ref<Script> script;

    double x_at(int p_side) const {
        Node3D *node = Object::cast_to<Node3D>(stand->node_at(p_side, route));
        return node != nullptr ? double(node->get_position().x) : 0.0;
    }

    bool open() {
        script = netw_test::minted_script(RUNNER_SOURCE);
        if (script.is_null()) {
            return false;
        }
        runner_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(RUNNER_ID), callable_mp_static(&build_runner));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(SERVER);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(A));
        Array args;
        args.push_back(String("Runner"));
        const RID made = server->spawn_registered(
            StringName(RUNNER_ID),
            args,
            owner.ptr()
        );
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        route = int(server->entity_get_route(made));
        stand->pump(8);
        if (stand->node_at(A, route) == nullptr) {
            return false;
        }
        faults = new netw_test::FaultWatch();
        idle_stand = stand;
        stand->loopback()->hold_inbound_packets(stand->peer(A));
        netw::gd::scene_tree()->connect(
            StringName("process_frame"),
            callable_mp_static(&idle_poll)
        );
        return true;
    }

    void tick_in_physics() {
        stand->session(SERVER)->clock_engine().force_step(1);
        stand->session(A)->clock_engine().force_step(1);
        stand->loopback()->advance_time(1000.0 / double(TICKRATE));
        stand->session(SERVER)->poll();
    }

    void bump_server() {
        Node3D *node = Object::cast_to<Node3D>(stand->node_at(SERVER, route));
        node->set_position(node->get_position() + Vector3(real_t(BUMP), 0, 0));
    }

    void close() {
        IndependentEvidence &evidence = independent_evidence();
        evidence.driven = true;
        if (idle_stand != nullptr) {
            netw::gd::scene_tree()->disconnect(
                StringName("process_frame"),
                callable_mp_static(&idle_poll)
            );
            idle_stand = nullptr;
        }
        if (faults != nullptr) {
            evidence.faults = faults->errors();
            delete faults;
            faults = nullptr;
        }
        delete stand;
        stand = nullptr;
        runner_script = nullptr;
        script = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                close();
                done = true;
                return false;
            }
            independent_evidence().seated = true;
        }
        if (frame == WARM_FRAMES) {
            bump_server();
        }
        if (frame == WARM_FRAMES + SETTLE_FRAMES) {
            IndependentEvidence &evidence = independent_evidence();
            Node *client = stand->node_at(A, route);
            evidence.replays = int64_t(client->get(StringName("replays")));
            evidence.replays_in_physics
                = int64_t(client->get(StringName("replays_in_physics")));
            evidence.queries_refused
                = int64_t(client->get(StringName("queries_refused")));
            evidence.motions_refused
                = int64_t(client->get(StringName("motions_refused")));
            evidence.server_x = x_at(SERVER);
            evidence.client_x = x_at(A);
            close();
            done = true;
            return false;
        }
        tick_in_physics();
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(IndependentScenario, independent_scenario);

} // namespace TestPredictIndependentFrameLaws

TEST_CASE(
    "[Networked][Predict][Frame] K-IR1 an INDEPENDENT correction that arrives "
    "between physics frames replays in the next physics frame"
) {
    using namespace TestPredictIndependentFrameLaws;
    const IndependentEvidence &evidence = independent_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.replays, int64_t(0));
    NETW_CHECK_EQ(evidence.replays_in_physics, evidence.replays);
    NETW_CHECK_EQ(evidence.queries_refused, int64_t(0));
    NETW_CHECK_EQ(evidence.motions_refused, int64_t(0));
    NETW_CHECK_EQ(evidence.faults, int64_t(0));
    NETW_CHECK_GT(evidence.client_x, evidence.server_x - BUMP * 0.5);
}

#endif
