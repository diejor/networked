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
#include "netw/api/promise.hpp"

using namespace godot;

namespace TestNetwSimReleaseFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;

constexpr const char *BALL_ID = "sim_release_ball";

constexpr const char *BALL_SOURCE = R"(extends Node3D

var sphere: RigidBody3D

var sphere_position: Vector3:
	get:
		return sphere.position
	set(value):
		sphere.position = value

var sphere_linear_velocity: Vector3:
	get:
		return sphere.linear_velocity
	set(value):
		sphere.linear_velocity = value

func _init() -> void:
	sphere = RigidBody3D.new()
	sphere.name = &"Sphere"
	sphere.gravity_scale = 0.0
	sphere.collision_layer = 0
	sphere.collision_mask = 0
	sphere.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.linear_damp = 0.0
	sphere.can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	sphere.add_child(shape)
	add_child(sphere)
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	e.simulation.bodies = [^"Sphere"]
	e.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	Netw.configure_property(self, &"sphere_position").broadcast()
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

const Vector3 PUSH(12.0, 0.0, 0.0);
const Vector3 WAKE(0.0, 0.0, 3.0);
constexpr double REST_X = 10.0;
constexpr int CLIENTS = 2;
constexpr int HOLDER = 0;
constexpr int OBSERVER = 1;
constexpr int TICKRATE = 30;
constexpr int FLIGHT_TICKS = 10;
constexpr int WARM_FRAMES = 12;
constexpr int WAKE_AFTER_FRAMES = 2;
constexpr int WAKE_READ_FRAMES = FLIGHT_TICKS + 4;
constexpr int WAKE_RUN_FRAMES = 30;
constexpr int SETTLE_FRAMES = 60;
constexpr int GIVE_UP_FRAMES = 400;

Script *ball_script = nullptr;

Node *build_ball(const Variant &p_name) {
    if (ball_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(ball_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct Peer {
    int64_t controller = -1;
    Vector3 position;
    Vector3 velocity;
};

struct ReleaseEvidence {
    bool driven = false;
    bool seated = false;
    bool rested = false;
    int64_t holder = 0;
    double holder_x_at_release = 0.0;
    double host_x_at_release = 0.0;
    Vector3 host_velocity_at_release;
    bool release_completed = false;
    bool woke = false;
    bool claim_completed = false;
    Vector3 holder_velocity_while_claiming;
    double holder_z_before_stop = 0.0;
    Peer peers[CLIENTS + 1];

    const Peer &at(int p_client) const {
        return peers[p_client + 1];
    }
};

ReleaseEvidence &rest_evidence() {
    static ReleaseEvidence evidence;
    return evidence;
}

ReleaseEvidence &wake_evidence() {
    static ReleaseEvidence evidence;
    return evidence;
}

class ReleaseScenario : public netw_test::FrameScenario {
    enum Phase {
        OPENING,
        WARMING,
        PUSHING,
        RELEASED,
        WAKING,
        STOPPED,
        DONE,
    };

    const bool wakes;
    Phase phase = OPENING;
    int frame = 0;
    int phase_frame = 0;
    int route = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Ref<NetwPromise> release;
    Ref<NetwPromise> claim;

    ReleaseEvidence &evidence() const {
        return wakes ? wake_evidence() : rest_evidence();
    }

    Node *ball_at(int p_client) const {
        return stand->node_at(p_client, route);
    }

    RigidBody3D *sphere_at(int p_client) const {
        Node *ball = ball_at(p_client);
        return ball == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(ball->get_node_or_null("Sphere"));
    }

    Ref<NetwEntity> entity_at(int p_client) const {
        return NetwEntity::of(ball_at(p_client));
    }

    bool open() {
        script = netw_test::minted_script(BALL_SOURCE);
        if (script.is_null()) {
            return false;
        }
        ball_script = script.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(BALL_ID), callable_mp_static(&build_ball));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(BALL_ID));
        const RID made
            = server->spawn_registered(StringName(BALL_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        for (int client = -1; client < CLIENTS; ++client) {
            if (sphere_at(client) == nullptr) {
                return false;
            }
        }
        const int64_t holder = stand->peer_id(HOLDER);
        entity_at(-1)->set_controller(holder);
        stand->pump(4);
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(43);
        flight->set_latency_ms(double(FLIGHT_TICKS) * 1000.0 / TICKRATE);
        stand->loopback()
            ->set_link_conditions(stand->peer(-1), flight, int(holder));
        evidence().holder = holder;
        evidence().seated = true;
        return true;
    }

    void close() {
        evidence().driven = true;
        release = Ref<NetwPromise>();
        claim = Ref<NetwPromise>();
        delete stand;
        stand = nullptr;
        ball_script = nullptr;
        script = Ref<Script>();
    }

    void rest_and_release() {
        ReleaseEvidence &seen = evidence();
        RigidBody3D *held = sphere_at(HOLDER);
        held->set_position(Vector3(REST_X, 0.0, 0.0));
        held->set_linear_velocity(Vector3());
        seen.rested = true;
        seen.holder_x_at_release = held->get_position().x;
        seen.host_x_at_release = sphere_at(-1)->get_position().x;
        seen.host_velocity_at_release = sphere_at(-1)->get_linear_velocity();
        release = entity_at(HOLDER)->release_authority();
    }

    void wake() {
        evidence().woke = true;
        claim = entity_at(HOLDER)->claim_authority();
        sphere_at(HOLDER)->set_linear_velocity(WAKE);
    }

    void read_final() {
        ReleaseEvidence &seen = evidence();
        seen.release_completed
            = release.is_valid() && release->get_is_completed();
        seen.claim_completed = claim.is_valid() && claim->get_is_completed();
        for (int client = -1; client < CLIENTS; ++client) {
            Peer &peer = seen.peers[client + 1];
            peer.controller = entity_at(client)->get_controller();
            peer.position = sphere_at(client)->get_position();
            peer.velocity = sphere_at(client)->get_linear_velocity();
        }
    }

    void enter(Phase p_phase) {
        phase = p_phase;
        phase_frame = 0;
    }

    void drive() {
        switch (phase) {
            case WARMING:
                if (phase_frame >= WARM_FRAMES) {
                    sphere_at(HOLDER)->set_linear_velocity(PUSH);
                    enter(PUSHING);
                }
                break;
            case PUSHING:
                if (sphere_at(HOLDER)->get_position().x >= REST_X) {
                    rest_and_release();
                    enter(RELEASED);
                }
                break;
            case RELEASED:
                if (wakes && phase_frame == WAKE_AFTER_FRAMES) {
                    wake();
                    enter(WAKING);
                } else if (!wakes && phase_frame >= SETTLE_FRAMES) {
                    enter(DONE);
                }
                break;
            case WAKING:
                if (phase_frame == WAKE_READ_FRAMES) {
                    evidence().holder_velocity_while_claiming
                        = sphere_at(HOLDER)->get_linear_velocity();
                }
                if (phase_frame >= WAKE_RUN_FRAMES) {
                    evidence().holder_z_before_stop
                        = sphere_at(HOLDER)->get_position().z;
                    sphere_at(HOLDER)->set_linear_velocity(Vector3());
                    enter(STOPPED);
                }
                break;
            case STOPPED:
                if (phase_frame >= SETTLE_FRAMES) {
                    enter(DONE);
                }
                break;
            default:
                break;
        }
    }

public:
    explicit ReleaseScenario(bool p_wakes) : wakes(p_wakes) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || phase == DONE) {
            return false;
        }
        if (phase == OPENING) {
            if (!open()) {
                close();
                phase = DONE;
                return false;
            }
            enter(WARMING);
        } else {
            drive();
        }
        if (phase == DONE || frame > GIVE_UP_FRAMES) {
            read_final();
            close();
            phase = DONE;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        ++phase_frame;
        return true;
    }
};

class RestReleaseScenario final : public ReleaseScenario {
public:
    RestReleaseScenario() : ReleaseScenario(false) {
    }
};

class WakeReleaseScenario final : public ReleaseScenario {
public:
    WakeReleaseScenario() : ReleaseScenario(true) {
    }
};

NETW_FRAME_SCENARIO(RestReleaseScenario, rest_release_scenario);
NETW_FRAME_SCENARIO(WakeReleaseScenario, wake_release_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] X11 a body released at rest continues from "
    "the releaser's final state on every peer, however far behind the "
    "session's copy was"
) {
    const ReleaseEvidence &evidence = rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.rested);
    NETW_CHECK_CLOSE(evidence.holder_x_at_release, REST_X, 0.001);
    NETW_CHECK_LT(evidence.host_x_at_release, REST_X - 1.0);
    NETW_CHECK_GT(evidence.host_velocity_at_release.x, 1.0);
    CHECK(evidence.release_completed);
    for (int client = -1; client < CLIENTS; ++client) {
        NETW_FORMAT_INT(client_text, client);
        CAPTURE(client_text);
        const Peer &peer = evidence.at(client);
        NETW_CHECK_EQ(peer.controller, int64_t(0));
        NETW_CHECK_CLOSE(peer.position.x, REST_X, 0.05);
        NETW_CHECK_CLOSE(peer.velocity.length(), 0.0, 0.01);
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] X11 a body that wakes while its release is "
    "pending stays with the waking peer, which keeps authoring, and every "
    "peer converges on it"
) {
    const ReleaseEvidence &evidence = wake_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.woke);
    CHECK(evidence.release_completed);
    CHECK(evidence.claim_completed);
    CHECK(evidence.holder_velocity_while_claiming.is_equal_approx(WAKE));
    NETW_CHECK_GT(evidence.holder_z_before_stop, 0.3);
    const Peer &holder = evidence.at(HOLDER);
    for (int client = -1; client < CLIENTS; ++client) {
        NETW_FORMAT_INT(client_text, client);
        CAPTURE(client_text);
        const Peer &peer = evidence.at(client);
        NETW_CHECK_EQ(peer.controller, evidence.holder);
        NETW_CHECK_CLOSE(peer.position.x, holder.position.x, 0.05);
        NETW_CHECK_CLOSE(peer.position.z, holder.position.z, 0.05);
    }
    NETW_CHECK_CLOSE(holder.position.z, evidence.holder_z_before_stop, 0.05);
}

} // namespace TestNetwSimReleaseFrameLaws

#endif
