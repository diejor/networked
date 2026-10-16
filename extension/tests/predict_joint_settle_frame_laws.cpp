#include "support/frame_drive.h"
#include "support/link.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/simulation.hpp"

using namespace godot;

namespace TestPredictJointSettleFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *WALKER_ID = "settle_walker";

constexpr const char *WALKER_SOURCE = R"(extends CharacterBody3D

var motion := 0
var speed := 0.0
var replayed := {}

func _init() -> void:
	var hull := CollisionShape3D.new()
	hull.shape = BoxShape3D.new()
	add_child(hull)
	motion_mode = CharacterBody3D.MOTION_MODE_FLOATING
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	e.prediction.recovery_policy = NetwPrediction.RECOVERY_POLICY_REBASE_REPLAY
	Netw.configure_property(self, &"position").state().on_spawn()
	Netw.configure_property(self, &"speed").state().on_spawn()
	Netw.configure_property(self, &"motion").input()

func _ready() -> void:
	var peer_layer := 1 if multiplayer.is_server() else 2
	collision_layer = peer_layer
	collision_mask = peer_layer

func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	velocity = Vector3(speed, 0.0, 0.0)
	move_and_slide()
	if not is_fresh:
		replayed[tick] = position.x
)";

constexpr int SERVER = -1;
constexpr int CLIENT = 0;
constexpr int TICKRATE = 30;
constexpr int BUMP_FRAME = 40;
constexpr int LAST_FRAME = 70;
constexpr double LATENCY_MS = 100.0;
constexpr double LEAD_START = 1.5;
constexpr double LEAD_SPEED = 1.5;
constexpr double TAIL_SPEED = 3.0;
constexpr double BUMP = 0.05;
constexpr double HULL = 1.0;
constexpr double STRIDE = TAIL_SPEED / 60.0;
constexpr double SLACK = 5.0e-3;

Script *walker_script = nullptr;

Node *build_walker(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(walker_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct SettleEvidence {
    bool driven = false;
    bool seated = false;
    int64_t blocked = 0;
    double deepest = 0.0;
    int64_t replay_frames = 0;
    double slowest_after_replay = 1.0e9;
    double fastest_after_replay = 0.0;
};

SettleEvidence &settle_evidence() {
    static SettleEvidence evidence;
    return evidence;
}

class SettleScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    int route_a = 0;
    int route_b = 0;
    int64_t bump_tick = 0;
    int64_t lead_replays = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;

    void watch_lead() {
        PhysicsBody3D *lead
            = Object::cast_to<PhysicsBody3D>(stand->node_at(CLIENT, route_a));
        const int64_t replays
            = Dictionary(lead->get(StringName("replayed"))).size();
        const bool replayed = replays > lead_replays;
        lead_replays = replays;
        if (!replayed || frame <= BUMP_FRAME) {
            return;
        }
        const Vector3 velocity
            = PhysicsServer3D::get_singleton()->body_get_state(
                lead->get_rid(),
                PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY
            );
        SettleEvidence &evidence = settle_evidence();
        evidence.replay_frames += 1;
        evidence.slowest_after_replay
            = std::min(evidence.slowest_after_replay, double(velocity.x));
        evidence.fastest_after_replay
            = std::max(evidence.fastest_after_replay, double(velocity.x));
    }

    RID spawn(Node *p_arena, const char *p_name, netw::NetwPlayer *p_owner) {
        NetwMultiplayer *server = stand->session(SERVER);
        Array args;
        args.push_back(String(p_name));
        const RID made
            = server->spawn_registered(StringName(WALKER_ID), args, p_owner);
        Node3D *built = Object::cast_to<Node3D>(server->entity_get_node(made));
        if (built == nullptr) {
            return RID();
        }
        const bool lead = p_owner != nullptr;
        built->set_position(Vector3(real_t(lead ? LEAD_START : 0.0), 0.0, 0.0));
        built->set(StringName("speed"), lead ? LEAD_SPEED : TAIL_SPEED);
        p_arena->add_child(built);
        return made;
    }

    void join(int p_side) {
        NetwMultiplayer *api = stand->session(p_side);
        const RID owner = api->entity_from_route(route_a);
        const RID member = api->entity_from_route(route_b);
        api->simulation_handle(owner)->simulate(api->entity_get_view(member));
        api->prediction_handle(owner)->set_reconcile_mode(
            netw::NetwPrediction::RECONCILE_JOINT
        );
    }

    bool open() {
        script = netw_test::minted_script(WALKER_SOURCE);
        if (script.is_null()) {
            return false;
        }
        walker_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(WALKER_ID), callable_mp_static(&build_walker));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(CLIENT)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(SERVER);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(CLIENT));
        const RID a = spawn(arena, "A", owner.ptr());
        const RID b = spawn(arena, "B", nullptr);
        if (!a.is_valid() || !b.is_valid()) {
            return false;
        }
        route_a = int(server->entity_get_route(a));
        route_b = int(server->entity_get_route(b));
        stand->pump(8);
        if (stand->node_at(CLIENT, route_a) == nullptr
            || stand->node_at(CLIENT, route_b) == nullptr) {
            return false;
        }
        join(SERVER);
        join(CLIENT);
        Ref<netw::NetwLinkConditions> slow = netw_test::link();
        slow->set_latency_ms(LATENCY_MS);
        stand->loopback()->set_link_conditions(
            stand->peer(CLIENT),
            slow,
            stand->peer_id(SERVER)
        );
        return true;
    }

    void bump() {
        Node3D *a = Object::cast_to<Node3D>(stand->node_at(SERVER, route_a));
        a->set_position(a->get_position() + Vector3(real_t(BUMP), 0.0, 0.0));
        bump_tick = stand->session(CLIENT)->clock_engine().get_tick();
    }

    void judge() {
        SettleEvidence &evidence = settle_evidence();
        const Dictionary a
            = stand->node_at(CLIENT, route_a)->get(StringName("replayed"));
        const Dictionary b
            = stand->node_at(CLIENT, route_b)->get(StringName("replayed"));
        const Array ticks = a.keys();
        for (int at = 0; at < ticks.size(); ++at) {
            const int64_t tick = int64_t(ticks[at]);
            const Variant before = int64_t(tick - 1);
            if (tick < bump_tick || !b.has(ticks[at]) || !a.has(before)
                || !b.has(before)) {
                continue;
            }
            const double a_was = double(a[before]);
            const double b_now = double(b[ticks[at]]);
            const double b_was = double(b[before]);
            if (b_now - b_was > STRIDE - SLACK) {
                continue;
            }
            evidence.blocked += 1;
            evidence.deepest
                = std::max(evidence.deepest, std::abs(a_was - b_now - HULL));
        }
    }

    void close() {
        settle_evidence().driven = true;
        delete stand;
        stand = nullptr;
        walker_script = nullptr;
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
            settle_evidence().seated = true;
        }
        if (frame == BUMP_FRAME) {
            bump();
        }
        if (frame == LAST_FRAME) {
            judge();
            close();
            done = true;
            return false;
        }
        watch_lead();
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(SettleScenario, settle_scenario);

} // namespace TestPredictJointSettleFrameLaws

TEST_CASE(
    "[Networked][Predict][Frame] K-RS2 in a JOINT replay without a physics "
    "step a character stops at the pose the one ahead of it replayed the "
    "tick before, and on a build that cannot flush it sees that pose late"
) {
    using namespace TestPredictJointSettleFrameLaws;
    const SettleEvidence &evidence = settle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.blocked, int64_t(0));
    if (netw::gd::can_flush_kinematic()) {
        NETW_CHECK_LT(evidence.deepest, SLACK);
    } else {
        NETW_CHECK_GT(evidence.deepest, SLACK);
    }
}

TEST_CASE(
    "[Networked][Predict][Frame] K-RS3 the live step after a JOINT replay "
    "moves a character by its last replayed tick and its live tick together, "
    "so the replayed motion reaches the physics server as velocity"
) {
    using namespace TestPredictJointSettleFrameLaws;
    const SettleEvidence &evidence = settle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.replay_frames, int64_t(0));
    if (netw::gd::can_flush_kinematic()) {
        NETW_CHECK_CLOSE(
            evidence.slowest_after_replay,
            2.0 * LEAD_SPEED,
            SLACK * 60.0
        );
        NETW_CHECK_CLOSE(
            evidence.fastest_after_replay,
            2.0 * LEAD_SPEED,
            SLACK * 60.0
        );
    }
}

#endif
