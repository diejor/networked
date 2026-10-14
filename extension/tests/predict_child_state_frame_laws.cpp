#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestPredictChildStateFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *RUNNER_ID = "child_state_runner";

constexpr const char *RUNNER_SOURCE = R"(extends Node3D

class Part extends Node:
	var charge := 0

	func _init() -> void:
		name = &"Part"
		Netw.configure_property(self, &"charge").state()

var beat := 0
var motion := 0
var part: Part

func _init() -> void:
	part = Part.new()
	add_child(part)
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"motion").input()

func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	beat += 1
	part.charge += 1
)";

constexpr int SERVER = -1;
constexpr int A = 0;
constexpr int TICKRATE = 30;
constexpr int WARM_FRAMES = 20;
constexpr int SETTLE_FRAMES = 30;
constexpr int BUMP = 100;

Script *runner_script = nullptr;

Node *build_runner(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(runner_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct Side {
    int64_t beat = 0;
    int64_t charge = 0;

    int64_t gap() const {
        return beat - charge;
    }
};

struct ChildStateEvidence {
    bool driven = false;
    bool seated = false;
    Side server;
    Side client;
};

ChildStateEvidence &child_state_evidence() {
    static ChildStateEvidence evidence;
    return evidence;
}

class ChildStateScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    int route = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;

    Side read(int p_side) const {
        Node *node = stand->node_at(p_side, route);
        Side side;
        side.beat = int64_t(node->get(StringName("beat")));
        side.charge = int64_t(
            node->get_node<Node>(NodePath("Part"))->get(StringName("charge"))
        );
        return side;
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
        return stand->node_at(A, route) != nullptr;
    }

    void bump_server() {
        Node *node = stand->node_at(SERVER, route);
        node->set(
            StringName("beat"),
            int64_t(node->get(StringName("beat"))) + BUMP
        );
        Node *part = node->get_node<Node>(NodePath("Part"));
        part->set(
            StringName("charge"),
            int64_t(part->get(StringName("charge"))) + BUMP
        );
    }

    void close() {
        child_state_evidence().driven = true;
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
            child_state_evidence().seated = true;
        }
        if (frame == WARM_FRAMES) {
            bump_server();
        }
        if (frame == WARM_FRAMES + SETTLE_FRAMES) {
            ChildStateEvidence &evidence = child_state_evidence();
            evidence.server = read(SERVER);
            evidence.client = read(A);
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ChildStateScenario, child_state_scenario);

} // namespace TestPredictChildStateFrameLaws

TEST_CASE(
    "[Networked][Predict][Frame] K-PC1 a predicting client corrected by the "
    "server agrees with it on a state property declared on a child node"
) {
    using namespace TestPredictChildStateFrameLaws;
    const ChildStateEvidence &evidence = child_state_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.client.beat, int64_t(BUMP));
    NETW_CHECK_EQ(evidence.server.gap(), int64_t(0));
    NETW_CHECK_EQ(evidence.client.gap(), evidence.server.gap());
    NETW_CHECK_GE(evidence.client.charge, evidence.server.charge - 4);
}

#endif
