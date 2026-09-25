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

namespace TestNetwClaimImageFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;

constexpr const char *BALL_ID = "claim_image_ball";

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
	Netw.configure_property(self, &"sphere_position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

constexpr auto YIELDABLE = netw::entity::Control::HOLD_YIELDABLE;
const Vector3 PUSH(0.0, 0.0, 4.0);
const Vector3 SIDE(4.0, 0.0, 0.0);
constexpr int CLIENTS = 2;
constexpr int TICKRATE = 30;
constexpr int FLIGHT_TICKS = 4;
constexpr int WARM_FRAMES = 30;
constexpr int WATCH_FRAMES = TICKRATE * 2;
constexpr int HANDOFF_FRAMES = 20;
constexpr int HANDED_BACK = WARM_FRAMES + WATCH_FRAMES + HANDOFF_FRAMES;
constexpr int REPLAY_BY = HANDED_BACK + HANDOFF_FRAMES;

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

struct ImageEvidence {
    bool driven = false;
    bool seated = false;
    int server_took = -1;
    int server_granted = -1;
    int observer_took = -1;
    int claimant_granted = -1;
    bool server_took_before_grant = false;
    bool taken_between = false;
    bool retaken = false;
    Vector3 before_replay;
    Vector3 after_replay;
};

ImageEvidence &image_evidence() {
    static ImageEvidence evidence;
    return evidence;
}

class ImageScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int route = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Ref<NetwPromise> promise;

    static PackedByteArray &first_tenure_image() {
        static PackedByteArray image;
        return image;
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

    bool pushed(int p_client) const {
        return sphere_at(p_client)->get_linear_velocity().z > PUSH.z * 0.5;
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
            = netw::LocalLinkConditions::create(43);
        flight->set_latency_ms(double(FLIGHT_TICKS) * 1000.0 / TICKRATE);
        for (int peer = -1; peer < CLIENTS; ++peer) {
            stand->loopback()->set_link_conditions(stand->peer(peer), flight);
        }
        image_evidence().seated = true;
        return true;
    }

    void close() {
        image_evidence().driven = true;
        promise = Ref<NetwPromise>();
        delete stand;
        stand = nullptr;
        ball_script = nullptr;
        script = Ref<Script>();
    }

    void watch(int p_since) {
        ImageEvidence &evidence = image_evidence();
        const int64_t claimant = stand->peer_id(0);
        if (evidence.server_granted < 0
            && entity_at(-1)->get_controller() == claimant) {
            evidence.server_granted = p_since;
        }
        if (evidence.claimant_granted < 0 && promise->get_is_settled()) {
            evidence.claimant_granted = p_since;
        }
        if (evidence.server_took < 0 && pushed(-1)) {
            evidence.server_took = p_since;
            evidence.server_took_before_grant = evidence.server_granted < 0;
        }
        if (evidence.observer_took < 0 && pushed(1)) {
            evidence.observer_took = p_since;
        }
    }

    void replay_first_tenure() {
        ImageEvidence &evidence = image_evidence();
        evidence.retaken = true;
        evidence.before_replay = sphere_at(-1)->get_linear_velocity();
        netw::session::ClaimImage stale;
        if (netw::session::frame_read(first_tenure_image(), stale)) {
            entity_at(-1)->_handle_claim_image(stand->peer_id(0), stale);
        }
        evidence.after_replay = sphere_at(-1)->get_linear_velocity();
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
            promise = entity_at(0)->claim_authority(YIELDABLE);
            sphere_at(0)->set_linear_velocity(PUSH);
            stand->step_ticks(1);
        } else if (frame <= WARM_FRAMES + WATCH_FRAMES) {
            stand->step_ticks(1);
            watch(frame - WARM_FRAMES);
        } else if (frame == WARM_FRAMES + WATCH_FRAMES + 1) {
            first_tenure_image() = entity_at(0)->claim_image(frame);
            entity_at(1)->claim_authority();
            sphere_at(1)->set_linear_velocity(SIDE);
            stand->step_ticks(1);
        } else if (frame == HANDED_BACK) {
            image_evidence().taken_between
                = entity_at(-1)->get_controller() == stand->peer_id(1);
            entity_at(1)->release_authority(stand->peer_id(0));
            stand->step_ticks(1);
        } else if (frame < REPLAY_BY) {
            if (frame > HANDED_BACK && !image_evidence().retaken
                && entity_at(-1)->get_controller() == stand->peer_id(0)) {
                replay_first_tenure();
            }
            stand->step_ticks(1);
        } else {
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ImageScenario, image_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] CI1 a pending claimant's state reaches the "
    "session one flight after the claim and before its grant returns, and "
    "never ahead of the session's own decision"
) {
    const ImageEvidence &evidence = image_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.server_took >= 0);
    REQUIRE(evidence.claimant_granted >= 0);
    CHECK_FALSE(evidence.server_took_before_grant);
    NETW_CHECK_LE(evidence.server_took, FLIGHT_TICKS + 2);
    NETW_CHECK_LT(evidence.server_took, evidence.claimant_granted);
}

TEST_CASE(
    "[Networked][Sim][Frame] CI2 a third peer takes a pending claimant's "
    "state within two flights, the same as any row the claimant would send"
) {
    const ImageEvidence &evidence = image_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.observer_took >= 0);
    NETW_CHECK_LE(evidence.observer_took, 2 * FLIGHT_TICKS + 2);
}

TEST_CASE(
    "[Networked][Sim][Frame] CI3 a state a peer sent under an earlier "
    "control of the same entity is refused after another peer held it in "
    "between, whoever controls it now"
) {
    const ImageEvidence &evidence = image_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.taken_between);
    REQUIRE(evidence.retaken);
    REQUIRE(evidence.before_replay.is_equal_approx(SIDE));
    CHECK(evidence.after_replay.is_equal_approx(SIDE));
}

} // namespace TestNetwClaimImageFrameLaws

#endif
