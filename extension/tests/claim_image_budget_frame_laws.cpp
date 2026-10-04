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
#include "netw/replication_send.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestNetwClaimImageBudgetFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *BALL_ID = "claim_image_budget_ball";

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
	e.simulation.replicas = NetwSimulation.REPLICAS_ACTIVE
	Netw.configure_property(self, &"sphere_position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

constexpr auto YIELDABLE = netw::entity::Control::HOLD_YIELDABLE;
const Vector3 PUSH(0.0, 0.0, 4.0);
constexpr int CLIENTS = 1;
constexpr int TICKRATE = 30;
constexpr int FLIGHT_TICKS = 4;
constexpr int WARM_FRAMES = 30;
constexpr int WATCH_FRAMES = 30;
constexpr int SETTLE_FRAMES = 30;
constexpr int FREE_CLAIM = WARM_FRAMES;
constexpr int FREE_ENDS = FREE_CLAIM + WATCH_FRAMES;
constexpr int RELEASED = FREE_ENDS + 1;
constexpr int DEBT_CLAIM = RELEASED + SETTLE_FRAMES;
constexpr int DEBT_ENDS = DEBT_CLAIM + WATCH_FRAMES;
constexpr int64_t DEBT_BYTES = 1 << 20;

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

struct BudgetEvidence {
    bool driven = false;
    bool seated = false;
    int64_t images_free = -1;
    int64_t images_indebted = -1;
};

BudgetEvidence &budget_evidence() {
    static BudgetEvidence evidence;
    return evidence;
}

class BudgetScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int route = 0;
    int64_t counted_from = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;

    Node *ball_at(int p_client) const {
        return stand->node_at(p_client, route);
    }

    RigidBody3D *sphere_at(int p_client) const {
        Node *ball = ball_at(p_client);
        return ball == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(ball->get_node_or_null("Sphere"));
    }

    Ref<NetwEntity> claimant_view() const {
        return NetwEntity::of(ball_at(0));
    }

    int64_t images_at_server() const {
        static const int64_t channel
            = netw::wire::builtin_channel(StringName("CLAIM_IMAGE"));
        const Dictionary snapshot = stand->session(-1)->attribution_snapshot();
        const Dictionary by_peer
            = snapshot.get(StringName("frames_in_by_channel"), Dictionary());
        const Dictionary by_channel
            = by_peer.get(int64_t(stand->peer_id(0)), Dictionary());
        return int64_t(by_channel.get(channel, int64_t(0)));
    }

    void indebt_claimant() {
        NetwMultiplayer *claimant = stand->session(0);
        netw::ReplicationSend *send
            = NetwMultiplayer::row_send_of(claimant->get_replication_plane());
        if (send != nullptr) {
            send->charge(stand->peer_id(-1), DEBT_BYTES);
        }
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
            = netw::LocalLinkConditions::create(47);
        flight->set_latency_ms(double(FLIGHT_TICKS) * 1000.0 / TICKRATE);
        for (int peer = -1; peer < CLIENTS; ++peer) {
            stand->loopback()->set_link_conditions(stand->peer(peer), flight);
        }
        budget_evidence().seated = true;
        return true;
    }

    void close() {
        budget_evidence().driven = true;
        delete stand;
        stand = nullptr;
        ball_script = nullptr;
        script = Ref<Script>();
    }

    void claim() {
        counted_from = images_at_server();
        claimant_view()->claim_authority(YIELDABLE);
        sphere_at(0)->set_linear_velocity(PUSH);
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
            ++frame;
            return true;
        }
        if (frame == FREE_CLAIM) {
            claim();
        } else if (frame == FREE_ENDS) {
            budget_evidence().images_free = images_at_server() - counted_from;
        } else if (frame == RELEASED) {
            claimant_view()->release_authority();
        } else if (frame >= DEBT_CLAIM && frame <= DEBT_ENDS) {
            indebt_claimant();
            if (frame == DEBT_CLAIM) {
                claim();
            } else if (frame == DEBT_ENDS) {
                budget_evidence().images_indebted
                    = images_at_server() - counted_from;
            }
        } else if (frame > DEBT_ENDS) {
            close();
            frame = -1;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(BudgetScenario, budget_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] CB1 a claimant whose budget to a peer is spent "
    "sends that peer no claim image, and one with budget to spare does"
) {
    const BudgetEvidence &evidence = budget_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.images_free, int64_t(0));
    NETW_CHECK_EQ(evidence.images_indebted, int64_t(0));
}

} // namespace TestNetwClaimImageBudgetFrameLaws

#endif
