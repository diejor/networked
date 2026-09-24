#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwClaimsCostFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using Hold = netw::entity::Control::Hold;

constexpr const char *CUBE_ID = "claims_cost_cube";

constexpr const char *CUBE_SOURCE = R"(extends Node3D

const LIMIT := 64.0

var sphere: RigidBody3D
var held_by := 0

var sphere_position: Vector3:
	get:
		return sphere.position
	set(value):
		sphere.position = value

var sphere_quaternion: Quaternion:
	get:
		return sphere.quaternion
	set(value):
		sphere.quaternion = value

var sphere_linear_velocity: Vector3:
	get:
		return sphere.linear_velocity
	set(value):
		sphere.linear_velocity = value

var sphere_angular_velocity: Vector3:
	get:
		return sphere.angular_velocity
	set(value):
		sphere.angular_velocity = value

var sphere_sleeping: bool:
	get:
		return sphere.sleeping
	set(value):
		sphere.sleeping = value

func _init() -> void:
	sphere = RigidBody3D.new()
	sphere.name = &"Sphere"
	sphere.gravity_scale = 0.0
	sphere.collision_layer = 0
	sphere.collision_mask = 0
	sphere.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.linear_damp = 3.0
	sphere.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.angular_damp = 3.0
	var shape := CollisionShape3D.new()
	var ball := SphereShape3D.new()
	ball.radius = 0.5
	shape.shape = ball
	sphere.add_child(shape)
	add_child(sphere)
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	e.simulation.bodies = [^"Sphere"]
	e.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	e.simulation.claim_on_contact = true
	e.simulation.release_on_rest = 0.3
	Netw.configure_property(self, &"held_by").broadcast()
	Netw.configure_property(self, &"sphere_position").broadcast() \
			.heartbeat(60) \
			.quantize(NetwQuantizeScalar.new().bits(20).limits(-LIMIT, LIMIT))
	Netw.configure_property(self, &"sphere_quaternion").broadcast() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"sphere_angular_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"sphere_sleeping").broadcast()
)";

constexpr int64_t CONTROL_REQUEST = 21;
constexpr int64_t CONTROL_APPLY = 22;
constexpr int CLIENTS = 2;
constexpr int A = 0;
constexpr int B = 1;
constexpr int TICKRATE = 30;
constexpr int FLIGHT_TICKS = 3;
constexpr int WARM_FRAMES = 90;
constexpr int CUBES = 5;
constexpr int PUSHER = 0;
constexpr int CONTESTED = CUBES - 1;
constexpr int QUIET_FRAMES = 90;
constexpr int GRANT_FRAMES = 10;
constexpr int BURST_FRAMES = 240;
constexpr int SHOVE_FRAMES = 240;
constexpr int HANDOFF_EVERY = 4;
constexpr int HANDOFFS = 20;
constexpr int SETTLE_FRAMES = 60;
const Vector3 PUSH(12.0, 0.0, 0.0);
const Vector3 SHOVE(3.0, 0.0, 0.0);

enum Phase {
    QUIET,
    BURST,
    SHOVE_PHASE,
    HANDOFF,
    PHASES,
};

constexpr int BURST_AT = QUIET_FRAMES;
constexpr int PUSH_AT = BURST_AT + GRANT_FRAMES;
constexpr int SHOVE_AT = PUSH_AT + BURST_FRAMES;
constexpr int HANDOFF_AT = SHOVE_AT + SHOVE_FRAMES;
constexpr int HANDOFF_END = HANDOFF_AT + HANDOFF_EVERY * HANDOFFS;
constexpr int END_AT = HANDOFF_END + SETTLE_FRAMES;

Script *cube_script = nullptr;

Node *build_cube(const Variant &p_name) {
    if (cube_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(cube_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct Queues {
    int64_t pending_ops = 0;
    int64_t parked_opens = 0;
    int64_t held_installs = 0;
    int64_t claimed = 0;

    void widen(const Queues &p_other) {
        pending_ops = MAX(pending_ops, p_other.pending_ops);
        parked_opens = MAX(parked_opens, p_other.parked_opens);
        held_installs = MAX(held_installs, p_other.held_installs);
        claimed = MAX(claimed, p_other.claimed);
    }
};

struct PhaseEvidence {
    int64_t control_bytes = 0;
    Queues peak;
    Queues settled;
};

struct CostEvidence {
    bool driven = false;
    bool seated = false;
    int64_t burst_claimed = 0;
    int64_t handoff_controller = -1;
    int64_t last_requester = -1;
    double shove_gap = -1.0;
    bool shove_claimed = false;
    int64_t last_controller[CUBES] = {};
    int controller_changes[CUBES] = {};
    PhaseEvidence phases[PHASES];
};

CostEvidence &evidence() {
    static CostEvidence held;
    return held;
}

class CostScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int routes[CUBES] = {};
    int64_t opened = 0;

    Node *cube_at(int p_client, int p_index) const {
        return stand->node_at(p_client, routes[p_index]);
    }

    RigidBody3D *sphere_at(int p_client, int p_index) const {
        Node *cube = cube_at(p_client, p_index);
        return cube == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(cube->get_node_or_null("Sphere"));
    }

    Ref<NetwEntity> entity_at(int p_client, int p_index) const {
        return NetwEntity::of(cube_at(p_client, p_index));
    }

    void delay(int p_receiver, int p_sender, int p_seed) {
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(p_seed);
        flight->set_latency_ms(double(FLIGHT_TICKS) * 1000.0 / TICKRATE);
        stand->loopback()->set_link_conditions(
            stand->peer(p_receiver),
            flight,
            stand->peer_id(p_sender)
        );
    }

    int64_t control_bytes_now() const {
        int64_t spent = 0;
        for (int client = -1; client < CLIENTS; ++client) {
            NetwMultiplayer *session = stand->session(client);
            const Dictionary out = session->attribution_snapshot()["bytes_out"];
            const Array peers = out.values();
            for (int at = 0; at < peers.size(); ++at) {
                const Dictionary channels = peers[at];
                const Array ids = channels.keys();
                for (int which = 0; which < ids.size(); ++which) {
                    const int64_t channel = ids[which];
                    if (channel == CONTROL_REQUEST
                        || channel == CONTROL_APPLY) {
                        spent += int64_t(channels[ids[which]]);
                    }
                }
            }
        }
        return spent;
    }

    Queues queues_now() const {
        Queues seen;
        for (int client = -1; client < CLIENTS; ++client) {
            NetwMultiplayer *session = stand->session(client);
            seen.parked_opens += session->get_replication_plane()
                                     ->get_sync_pipeline()
                                     ->row_send_under_test()
                                     ->reader_book()
                                     .parked_count();
            for (int index = 0; index < CUBES; ++index) {
                const Ref<NetwEntity> entity = entity_at(client, index);
                seen.pending_ops += entity->pending_ops_under_test();
                if (index != PUSHER && entity->get_controller() != 0) {
                    seen.claimed += 1;
                }
                const netw::sim::Row *row
                    = session->sim_row_of(entity->get_rid_handle());
                if (row != nullptr) {
                    seen.held_installs += int64_t(row->installs.held.size());
                }
            }
        }
        return seen;
    }

    void count_changes() {
        CostEvidence &seen = evidence();
        for (int index = 0; index < CUBES; ++index) {
            const int64_t now = entity_at(-1, index)->get_controller();
            if (now != seen.last_controller[index]) {
                seen.controller_changes[index] += 1;
                seen.last_controller[index] = now;
            }
        }
    }

    bool spawn() {
        NetwMultiplayer *server = stand->session(-1);
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        for (int index = 0; index < CUBES; ++index) {
            Array args;
            args.push_back(String(CUBE_ID) + String::num_int64(index));
            const RID made
                = server->spawn_registered(StringName(CUBE_ID), args, nullptr);
            Node *built = server->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
            stand->pump(4);
            routes[index] = int(server->entity_get_route(made));
        }
        stand->pump(8);
        for (int index = 0; index < CUBES; ++index) {
            for (int client = -1; client < CLIENTS; ++client) {
                RigidBody3D *sphere = sphere_at(client, index);
                if (sphere == nullptr) {
                    return false;
                }
                const uint32_t bit = 1u << uint32_t(client + 2);
                sphere->set_collision_layer(bit);
                sphere->set_collision_mask(bit);
            }
            const Vector3 at = index == PUSHER
                ? Vector3(-3.0, 0.0, 0.0)
                : Vector3(1.05 * (index - 1), 0.0, 0.0);
            sphere_at(-1, index)->set_position(at);
        }
        return true;
    }

    bool open() {
        script = netw_test::minted_script(CUBE_SOURCE);
        if (script.is_null()) {
            return false;
        }
        cube_script = script.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        if (!spawn()) {
            return false;
        }
        delay(-1, A, 71);
        delay(A, -1, 72);
        delay(-1, B, 73);
        delay(B, -1, 74);
        stand->pump(4);
        evidence().seated = true;
        return true;
    }

    void close() {
        evidence().driven = true;
        delete stand;
        stand = nullptr;
        cube_script = nullptr;
        script = Ref<Script>();
    }

    static Phase phase_of(int p_step) {
        if (p_step < BURST_AT) {
            return QUIET;
        }
        if (p_step < SHOVE_AT) {
            return BURST;
        }
        return p_step < HANDOFF_AT ? SHOVE_PHASE : HANDOFF;
    }

    void close_phase(Phase p_phase) {
        PhaseEvidence &seen = evidence().phases[p_phase];
        const int64_t now = control_bytes_now();
        seen.control_bytes = now - opened;
        seen.settled = queues_now();
        opened = now;
    }

    void drive(int p_step) {
        CostEvidence &seen = evidence();
        if (p_step == 0) {
            opened = control_bytes_now();
        }
        if (p_step == BURST_AT) {
            close_phase(QUIET);
            entity_at(-1, PUSHER)->set_controller(stand->peer_id(A));
        }
        if (p_step == PUSH_AT) {
            sphere_at(A, PUSHER)->set_linear_velocity(PUSH);
        }
        if (p_step == SHOVE_AT) {
            close_phase(BURST);
            seen.shove_gap = sphere_at(A, PUSHER)->get_position().distance_to(
                sphere_at(A, PUSHER + 1)->get_position()
            );
            sphere_at(A, PUSHER)->set_linear_velocity(SHOVE);
        }
        if (p_step == HANDOFF_AT) {
            close_phase(SHOVE_PHASE);
        }
        if (p_step >= HANDOFF_AT && p_step < HANDOFF_END) {
            const int turn = (p_step - HANDOFF_AT) / HANDOFF_EVERY;
            const int beat = (p_step - HANDOFF_AT) % HANDOFF_EVERY;
            const int asker = turn % 2 == 0 ? A : B;
            if (beat == 0) {
                entity_at(asker, CONTESTED)
                    ->claim_authority(Hold::HOLD_EXCLUSIVE);
                seen.last_requester = stand->peer_id(asker);
            } else if (beat == 1) {
                entity_at(asker, CONTESTED)
                    ->claim_authority(Hold::HOLD_YIELDABLE);
            }
        }
        const Queues now = queues_now();
        const Phase phase = phase_of(p_step);
        seen.phases[phase].peak.widen(now);
        if (phase == BURST) {
            seen.burst_claimed = MAX(seen.burst_claimed, now.claimed);
            count_changes();
        }
        if (phase == SHOVE_PHASE
            && entity_at(-1, PUSHER + 1)->get_controller()
                == stand->peer_id(A)) {
            seen.shove_claimed = true;
        }
    }

    void read_final() {
        close_phase(HANDOFF);
        evidence().handoff_controller
            = entity_at(-1, CONTESTED)->get_controller();
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
        const int step = frame - WARM_FRAMES;
        if (step >= 0) {
            drive(step);
        }
        if (step >= END_AT) {
            read_final();
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(CostScenario, claims_cost_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a quiet scene of free bodies sends no control "
    "traffic, a contact burst is claimed and released once, a resting "
    "contact pushed again claims again, and a run of handoffs leaves every "
    "claim queue drained"
) {
    const CostEvidence &seen = evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    NETW_CHECK_EQ(seen.phases[QUIET].control_bytes, int64_t(0));
    NETW_CHECK_GT(seen.burst_claimed, int64_t(0));
    NETW_CHECK_LT(seen.shove_gap, 1.0);
    CHECK(seen.shove_claimed);
    for (int index = 1; index < CUBES; ++index) {
        NETW_FORMAT_INT(cube_text, index);
        CAPTURE(cube_text);
        NETW_CHECK_EQ(seen.controller_changes[index], 2);
    }
    NETW_CHECK_GT(seen.phases[HANDOFF].peak.pending_ops, int64_t(0));
    for (int phase = 0; phase < PHASES; ++phase) {
        NETW_FORMAT_INT(phase_text, phase);
        CAPTURE(phase_text);
        const Queues &settled = seen.phases[phase].settled;
        NETW_CHECK_EQ(settled.pending_ops, int64_t(0));
        NETW_CHECK_EQ(settled.parked_opens, int64_t(0));
        NETW_CHECK_EQ(settled.held_installs, int64_t(0));
        NETW_CHECK_EQ(
            settled.claimed,
            int64_t(phase == HANDOFF ? CLIENTS + 1 : 0)
        );
    }
    NETW_CHECK_EQ(seen.handoff_controller, seen.last_requester);
}

} // namespace TestNetwClaimsCostFrameLaws

#endif
