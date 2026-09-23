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
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimClaimFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::sim::Mode;

constexpr const char *BALL_ID = "sim_claim_ball";

constexpr const char *BALL_SOURCE = R"(extends Node3D

var sphere: RigidBody3D
var modes := 0
var changes := 0

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
	e.simulation.mode_changed.connect(func(_was, _now): modes += 1)
	e.control_changed.connect(func(_was, _now): changes += 1)
	Netw.configure_property(self, &"sphere_position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

const Vector3 DRIFT(1.0, 0.0, 0.0);
const Vector3 GRAB_A(0.0, 0.0, 4.0);
const Vector3 GRAB_B(0.0, 0.0, -4.0);
constexpr int CLIENTS = 2;
constexpr int TICKRATE = 30;
constexpr int FLIGHT_TICKS = 4;
constexpr int WARM_FRAMES = 12;
constexpr int PENDING_READ_FRAMES = 2;
constexpr int SETTLE_FRAMES = 40;

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

struct Claimant {
    bool ran_ahead = false;
    bool announced_before_return = false;
    Mode claimed_mode = Mode::NONE;
    Vector3 claimed_position;
    Vector3 pending_position;
    Vector3 pending_velocity;
    bool settled = false;
    bool failed = false;
    int code = OK;
    Vector3 settled_velocity;
    Mode settled_mode = Mode::NONE;
    bool controlled_after = false;
    int64_t changes = 0;
    int64_t controller = -1;
    Vector3 final_velocity;
};

struct ClaimEvidence {
    bool driven = false;
    bool seated = false;
    Claimant claimants[CLIENTS];
    int64_t winner = 0;
    int64_t server_controller = -1;
    Vector3 server_velocity;
};

ClaimEvidence &claim_evidence() {
    static ClaimEvidence evidence;
    return evidence;
}

class ClaimScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int route = 0;
    int settle_until = -1;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Ref<NetwPromise> promises[CLIENTS];

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

    Mode mode_at(int p_client) const {
        const netw::sim::Row *row = stand->session(p_client)->sim_row_of(
            entity_at(p_client)->get_rid_handle()
        );
        return row == nullptr ? Mode::NONE : row->mode;
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
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(41);
        flight->set_latency_ms(double(FLIGHT_TICKS) * 1000.0 / TICKRATE);
        for (int client = 0; client < CLIENTS; ++client) {
            stand->loopback()->set_link_conditions(
                stand->peer(-1),
                flight,
                stand->peer_id(client)
            );
        }
        sphere_at(-1)->set_linear_velocity(DRIFT);
        claim_evidence().seated = true;
        return true;
    }

    void close() {
        claim_evidence().driven = true;
        for (Ref<NetwPromise> &promise : promises) {
            promise = Ref<NetwPromise>();
        }
        delete stand;
        stand = nullptr;
        ball_script = nullptr;
        script = Ref<Script>();
    }

    void claim(int p_client, const Vector3 &p_impulse) {
        Claimant &seen = claim_evidence().claimants[p_client];
        const int64_t modes_before = int64_t(ball_at(p_client)->get("modes"));
        promises[p_client] = entity_at(p_client)->request_control();
        seen.ran_ahead = entity_at(p_client)->get_is_controlled_locally();
        seen.claimed_mode = mode_at(p_client);
        seen.announced_before_return
            = int64_t(ball_at(p_client)->get("modes")) > modes_before;
        sphere_at(p_client)->set_linear_velocity(p_impulse);
        seen.claimed_position = sphere_at(p_client)->get_position();
    }

    void read_pending() {
        for (int client = 0; client < CLIENTS; ++client) {
            Claimant &seen = claim_evidence().claimants[client];
            seen.pending_velocity = sphere_at(client)->get_linear_velocity();
            seen.pending_position = sphere_at(client)->get_position();
        }
    }

    bool read_settled() {
        bool all = true;
        for (int client = 0; client < CLIENTS; ++client) {
            Claimant &seen = claim_evidence().claimants[client];
            if (seen.settled) {
                continue;
            }
            if (!promises[client]->get_is_settled()) {
                all = false;
                continue;
            }
            seen.settled = true;
            seen.failed = promises[client]->get_is_failed();
            seen.code = int(promises[client]->get_code());
            seen.settled_velocity = sphere_at(client)->get_linear_velocity();
            seen.settled_mode = mode_at(client);
            seen.controlled_after
                = entity_at(client)->get_is_controlled_locally();
        }
        return all;
    }

    void read_final() {
        ClaimEvidence &evidence = claim_evidence();
        evidence.winner = stand->peer_id(0);
        evidence.server_controller = entity_at(-1)->get_controller();
        evidence.server_velocity = sphere_at(-1)->get_linear_velocity();
        for (int client = 0; client < CLIENTS; ++client) {
            Claimant &seen = evidence.claimants[client];
            seen.changes = int64_t(ball_at(client)->get("changes"));
            seen.controller = entity_at(client)->get_controller();
            seen.final_velocity = sphere_at(client)->get_linear_velocity();
        }
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                close();
                frame = -1;
                return false;
            }
        } else if (frame < WARM_FRAMES) {
            stand->step_ticks(1);
        } else if (frame == WARM_FRAMES) {
            claim(0, GRAB_A);
            claim(1, GRAB_B);
            stand->step_ticks(1);
        } else if (settle_until < 0) {
            stand->step_ticks(1);
            if (frame == WARM_FRAMES + PENDING_READ_FRAMES) {
                read_pending();
            }
            if (read_settled()) {
                settle_until = frame + SETTLE_FRAMES;
            }
            if (frame > WARM_FRAMES + TICKRATE * 2) {
                settle_until = frame;
            }
        } else if (frame < settle_until) {
            stand->step_ticks(1);
        } else {
            read_final();
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ClaimScenario, claim_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] X09 two immediate claims on one free body in "
    "one tick run ahead at once, and the first the session hears wins"
) {
    const ClaimEvidence &evidence = claim_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int client = 0; client < CLIENTS; ++client) {
        NETW_FORMAT_INT(client_text, client);
        CAPTURE(client_text);
        const Claimant &seen = evidence.claimants[client];
        CHECK(seen.ran_ahead);
        CHECK(seen.announced_before_return);
        NETW_CHECK_EQ(int(seen.claimed_mode), int(Mode::AUTHORITY));
        CHECK(seen.settled);
        NETW_CHECK_EQ(seen.controller, evidence.winner);
    }
    NETW_CHECK_EQ(evidence.server_controller, evidence.winner);
    CHECK_FALSE(evidence.claimants[0].failed);
    CHECK(evidence.claimants[1].failed);
    NETW_CHECK_EQ(evidence.claimants[1].code, int(ERR_UNAUTHORIZED));
}

TEST_CASE(
    "[Networked][Sim][Frame] X09 rows that arrive while a grab is pending "
    "never undo it, and the winner's impulse is applied once everywhere"
) {
    const ClaimEvidence &evidence = claim_evidence();
    REQUIRE(evidence.driven);
    for (int client = 0; client < CLIENTS; ++client) {
        NETW_FORMAT_INT(client_text, client);
        CAPTURE(client_text);
        const Claimant &seen = evidence.claimants[client];
        const Vector3 moved = seen.pending_position - seen.claimed_position;
        NETW_CHECK_CLOSE(moved.x, 0.0, 0.001);
        NETW_CHECK_GT(Math::abs(moved.z), 0.05);
    }
    CHECK(evidence.claimants[0].pending_velocity.is_equal_approx(GRAB_A));
    CHECK(evidence.claimants[1].pending_velocity.is_equal_approx(GRAB_B));
    CHECK(evidence.server_velocity.is_equal_approx(GRAB_A));
    for (int client = 0; client < CLIENTS; ++client) {
        NETW_FORMAT_INT(client_text, client);
        CAPTURE(client_text);
        const Vector3 ended = evidence.claimants[client].final_velocity;
        CHECK(ended.is_equal_approx(GRAB_A));
    }
    NETW_CHECK_EQ(evidence.claimants[0].changes, 1);
}

TEST_CASE(
    "[Networked][Sim][Frame] X09 the loser installs the sample it retained, "
    "runs as an active copy again, and sees only the winner's decision"
) {
    const ClaimEvidence &evidence = claim_evidence();
    REQUIRE(evidence.driven);
    const Claimant &loser = evidence.claimants[1];
    CHECK(loser.settled_velocity.is_equal_approx(DRIFT));
    NETW_CHECK_EQ(int(loser.settled_mode), int(Mode::ACTIVE));
    CHECK_FALSE(loser.controlled_after);
    NETW_CHECK_EQ(loser.changes, 1);
}

} // namespace TestNetwSimClaimFrameLaws

#endif
