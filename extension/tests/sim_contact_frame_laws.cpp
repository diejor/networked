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
#include "netw/api/simulation_handle.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimContactFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using Hold = netw::entity::Control::Hold;

constexpr const char *CUBE_ID = "sim_contact_cube";

constexpr const char *CUBE_SOURCE = R"(extends Node3D

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
	sphere.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.angular_damp = 0.0
	sphere.can_sleep = false
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
	Netw.configure_property(self, &"sphere_position").broadcast()
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

enum class Kind {
    CHAIN,
    REFUSED,
    REST,
    WAKE,
    FENCE,
    PUSH,
    DOUBLE,
    AUTHORED,
};

constexpr const char *AUTHORED_ID = "sim_contact_authored";

constexpr int CLIENTS = 2;
constexpr int A = 0;
constexpr int B = 1;
constexpr int TICKRATE = 30;
constexpr int WARM_FRAMES = 12;
constexpr int CUBES = 5;
constexpr double REST_SECONDS = 0.1;
constexpr int REST_TICKS = 3;
constexpr int FLIGHT_TICKS = 6;
constexpr int FENCE_READ_FRAMES = 3;
constexpr int PUSH_READ_FRAMES = FLIGHT_TICKS * 3;
const Vector3 PUSH_VELOCITY(2.0, 0.0, 0.0);
constexpr int DOUBLE_FLIGHT_TICKS = 12;
constexpr int DOUBLE_BACK_FRAMES = 2;
constexpr int DOUBLE_AGAIN_FRAMES = 5;
constexpr int DOUBLE_READ_FRAMES = DOUBLE_FLIGHT_TICKS * 3;
constexpr int SETTLE_FRAMES = 80;
const Vector3 DRIFT(0.0, 0.0, 1.0);
const Vector3 WAKE_VELOCITY(0.0, 0.0, 2.0);
const Hold REST_HOLDS[CUBES] = {
    Hold::HOLD_YIELDABLE,
    Hold::HOLD_YIELDABLE,
    Hold::HOLD_EXCLUSIVE,
    Hold::HOLD_EXCLUSIVE,
    Hold::HOLD_YIELDABLE,
};
const Hold REST_FINAL_HOLDS[CUBES] = {
    Hold::HOLD_NONE,
    Hold::HOLD_YIELDABLE,
    Hold::HOLD_EXCLUSIVE,
    Hold::HOLD_EXCLUSIVE,
    Hold::HOLD_EXCLUSIVE,
};
const bool REST_FROZEN[CUBES] = { false, true, false, true, false };
constexpr int REGRAB = 4;

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

Node *build_authored(const Variant &p_name) {
    Node *made = build_cube(p_name);
    if (made != nullptr) {
        NetwEntity::ensure(made)->set_lifecycle(
            NetwEntity::LIFECYCLE_CONTROLLER
        );
    }
    return made;
}

struct Subject {
    int claimed_frame = -1;
    int granted_frame = -1;
    int releasing_frame = -1;
    bool ran_ahead = false;
    bool asleep_seen = false;
    int64_t server_controller = -1;
    int64_t local_controller = -1;
    int64_t server_hold = -1;
    Vector3 local_velocity;
    Vector3 local_position;
    Vector3 server_velocity;
    Vector3 server_position;
};

struct ContactEvidence {
    bool driven = false;
    bool seated = false;
    int64_t holder = 0;
    int64_t rival = 0;
    int64_t reclaims = 0;
    bool woke = false;
    bool grab_failed = false;
    Vector3 hit_velocity;
    int64_t installed_at_onset = -1;
    int onsets = 0;
    int first_onset_frame = -1;
    int second_onset_frame = -1;
    bool was_touching = false;
    int64_t first_fence = -1;
    int64_t final_fence = -1;
    int64_t fenced_total = -1;
    int64_t installed_in_contact = -1;
    int64_t fenced_in_contact = -1;
    bool touching_at_read = false;
    Subject subjects[CUBES];
};

ContactEvidence &evidence_of(Kind p_kind) {
    static ContactEvidence evidence[8];
    return evidence[int(p_kind)];
}

class ContactScenario : public netw_test::FrameScenario {
    const Kind kind;
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int routes[CUBES] = {};
    Ref<NetwPromise> grab_a;
    Ref<NetwPromise> grab_b;

    ContactEvidence &evidence() const {
        return evidence_of(kind);
    }

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

    const netw::sim::Row *row_at(int p_client, int p_index) const {
        return stand->session(p_client)->sim_row_of(
            entity_at(p_client, p_index)->get_rid_handle()
        );
    }

    void collide(int p_client, int p_index) {
        const uint32_t bit = 1u << uint32_t(p_client + 2);
        RigidBody3D *sphere = sphere_at(p_client, p_index);
        sphere->set_collision_layer(bit);
        sphere->set_collision_mask(bit);
    }

    void delay(
        int p_receiver,
        int p_sender,
        int p_seed,
        int p_ticks = FLIGHT_TICKS
    ) {
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(p_seed);
        flight->set_latency_ms(double(p_ticks) * 1000.0 / TICKRATE);
        stand->loopback()->set_link_conditions(
            stand->peer(p_receiver),
            flight,
            stand->peer_id(p_sender)
        );
    }

    int cube_count() const {
        switch (kind) {
            case Kind::CHAIN:
                return 4;
            case Kind::REFUSED:
            case Kind::FENCE:
            case Kind::PUSH:
            case Kind::DOUBLE:
            case Kind::AUTHORED:
                return 2;
            case Kind::REST:
                return CUBES;
            case Kind::WAKE:
                return 1;
        }
        return 0;
    }

    Vector3 start_of(int p_index) const {
        switch (kind) {
            case Kind::CHAIN:
            case Kind::REFUSED:
            case Kind::AUTHORED:
                return Vector3(0.95 * p_index, 0.0, 0.0);
            case Kind::FENCE:
            case Kind::PUSH:
            case Kind::DOUBLE:
                return Vector3(0.0, 0.0, -5.0 * p_index);
            default:
                return Vector3(3.0 * p_index, 0.0, 0.0);
        }
    }

    bool spawn() {
        NetwMultiplayer *server = stand->session(-1);
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        if (kind == Kind::AUTHORED) {
            stand->session(A)->session_submit_join(StringName("a"), Array());
            stand->pump(8);
        }
        for (int index = 0; index < cube_count(); ++index) {
            const bool authored = kind == Kind::AUTHORED && index == 0;
            NetwMultiplayer *spawner = authored ? stand->session(A) : server;
            Array args;
            args.push_back(String(CUBE_ID) + String::num_int64(index));
            const RID made = spawner->spawn_registered(
                StringName(authored ? AUTHORED_ID : CUBE_ID),
                args,
                nullptr
            );
            Node *built = spawner->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            (authored ? stand->arena_of(A) : arena)->add_child(built);
            stand->pump(4);
            routes[index] = int(spawner->entity_get_route(made));
        }
        stand->pump(8);
        for (int index = 0; index < cube_count(); ++index) {
            for (int client = -1; client < CLIENTS; ++client) {
                RigidBody3D *sphere = sphere_at(client, index);
                if (sphere == nullptr) {
                    return false;
                }
                const Ref<netw::NetwSimulationHandle> handle
                    = entity_at(client, index)->get_simulation();
                handle->set_claim_on_contact(
                    kind == Kind::CHAIN || kind == Kind::REFUSED
                    || kind == Kind::AUTHORED
                );
                if (kind == Kind::REST || kind == Kind::WAKE) {
                    handle->set_release_on_rest(REST_SECONDS);
                }
                if (kind == Kind::FENCE || kind == Kind::PUSH
                    || kind == Kind::DOUBLE) {
                    sphere->set_contact_monitor(true);
                    sphere->set_max_contacts_reported(4);
                }
            }
            const int writer = kind == Kind::AUTHORED && index == 0 ? A : -1;
            sphere_at(writer, index)->set_position(start_of(index));
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
        stand->teach(
            StringName(AUTHORED_ID),
            callable_mp_static(&build_authored)
        );
        stand->mount();
        if (!spawn()) {
            return false;
        }
        const int64_t holder = stand->peer_id(A);
        evidence().holder = holder;
        evidence().rival = stand->peer_id(B);
        switch (kind) {
            case Kind::CHAIN:
                entity_at(-1, 0)->set_controller(holder);
                break;
            case Kind::AUTHORED:
                break;
            case Kind::REFUSED:
                delay(-1, A, 51);
                break;
            case Kind::REST:
            case Kind::WAKE:
                delay(-1, A, 52);
                delay(A, -1, 53);
                break;
            case Kind::FENCE:
            case Kind::PUSH:
                entity_at(-1, 1)->set_controller(holder);
                sphere_at(-1, 0)->set_linear_velocity(DRIFT);
                delay(A, -1, 54);
                break;
            case Kind::DOUBLE:
                entity_at(-1, 1)->set_controller(holder);
                sphere_at(-1, 0)->set_linear_velocity(DRIFT);
                delay(A, -1, 55, DOUBLE_FLIGHT_TICKS);
                break;
        }
        stand->pump(4);
        evidence().seated = true;
        return true;
    }

    void close() {
        evidence().driven = true;
        grab_a = Ref<NetwPromise>();
        grab_b = Ref<NetwPromise>();
        delete stand;
        stand = nullptr;
        cube_script = nullptr;
        script = Ref<Script>();
    }

    void begin() {
        switch (kind) {
            case Kind::CHAIN:
            case Kind::AUTHORED:
                for (int index = 0; index < cube_count(); ++index) {
                    collide(A, index);
                }
                break;
            case Kind::REFUSED:
                grab_b = entity_at(B, 0)->claim_authority();
                grab_a = entity_at(A, 0)->claim_authority();
                collide(A, 0);
                collide(A, 1);
                break;
            case Kind::REST:
            case Kind::WAKE:
                for (int index = 0; index < cube_count(); ++index) {
                    const Hold hold = kind == Kind::WAKE
                        ? Hold::HOLD_YIELDABLE
                        : REST_HOLDS[index];
                    entity_at(A, index)->claim_authority(hold);
                    RigidBody3D *sphere = sphere_at(A, index);
                    if (kind == Kind::REST && REST_FROZEN[index]) {
                        sphere->set_freeze_mode(
                            RigidBody3D::FREEZE_MODE_KINEMATIC
                        );
                        sphere->set_freeze_enabled(true);
                    }
                    sphere->set_can_sleep(true);
                }
                break;
            case Kind::FENCE:
            case Kind::PUSH:
            case Kind::DOUBLE: {
                collide(A, 0);
                collide(A, 1);
                const Vector3 at = sphere_at(A, 0)->get_position();
                sphere_at(A, 1)->set_position(at - Vector3(0.98, 0.0, 0.0));
                sphere_at(A, 1)->set_linear_velocity(Vector3(6.0, 0.0, 0.0));
                break;
            }
        }
    }

    void observe(int p_step) {
        ContactEvidence &seen = evidence();
        for (int index = 0; index < cube_count(); ++index) {
            Subject &subject = seen.subjects[index];
            const Ref<NetwEntity> local = entity_at(A, index);
            if (subject.claimed_frame < 0
                && local->get_is_controlled_locally()) {
                subject.claimed_frame = p_step;
            }
            if (local->is_claim_running_ahead()) {
                subject.ran_ahead = true;
            }
            if (subject.granted_frame < 0
                && local->get_controller() == seen.holder) {
                subject.granted_frame = p_step;
                if (kind == Kind::REST && index == REGRAB) {
                    local->claim_authority(Hold::HOLD_EXCLUSIVE);
                }
            }
            const netw::sim::Row *row = row_at(A, index);
            if (subject.releasing_frame < 0 && row != nullptr
                && row->contact.rest.releasing) {
                subject.releasing_frame = p_step;
            }
        }
    }

    void hold_asleep(int p_step) {
        ContactEvidence &seen = evidence();
        for (int index = 0; index < cube_count(); ++index) {
            if (kind == Kind::WAKE && seen.woke) {
                continue;
            }
            RigidBody3D *sphere = sphere_at(A, index);
            sphere->set_sleeping(true);
            if (seen.subjects[index].granted_frame >= 0
                && sphere->is_sleeping()) {
                seen.subjects[index].asleep_seen = true;
            }
        }
        if (kind == Kind::WAKE && !seen.woke
            && seen.subjects[0].releasing_frame >= 0
            && p_step > seen.subjects[0].releasing_frame) {
            seen.woke = true;
            RigidBody3D *sphere = sphere_at(A, 0);
            sphere->set_sleeping(false);
            sphere->set_linear_velocity(WAKE_VELOCITY);
        }
        if (kind == Kind::WAKE && seen.woke
            && entity_at(A, 0)->is_claim_pending()) {
            seen.reclaims = 1;
        }
    }

    void read_final() {
        ContactEvidence &seen = evidence();
        seen.grab_failed = grab_a.is_valid() && grab_a->get_is_failed();
        for (int index = 0; index < cube_count(); ++index) {
            Subject &subject = seen.subjects[index];
            subject.server_controller = entity_at(-1, index)->get_controller();
            subject.server_hold = int64_t(entity_at(-1, index)->get_hold());
            subject.local_controller = entity_at(A, index)->get_controller();
            subject.local_velocity = sphere_at(A, index)->get_linear_velocity();
            subject.local_position = sphere_at(A, index)->get_position();
            subject.server_velocity
                = sphere_at(-1, index)->get_linear_velocity();
            subject.server_position = sphere_at(-1, index)->get_position();
        }
    }

    void push(int p_step) {
        ContactEvidence &seen = evidence();
        const netw::sim::Row *row = row_at(A, 0);
        if (row == nullptr) {
            return;
        }
        if (p_step == 1) {
            seen.installed_at_onset = row->installs.stats.installed;
        }
        if (p_step == PUSH_READ_FRAMES) {
            seen.installed_in_contact
                = row->installs.stats.installed - seen.installed_at_onset;
            seen.fenced_in_contact = row->installs.stats.fenced;
            seen.touching_at_read = !row->contact.touching.is_empty();
        }
        if (p_step > 0 && p_step <= PUSH_READ_FRAMES) {
            sphere_at(A, 1)->set_linear_velocity(PUSH_VELOCITY);
        }
    }

    void touch_twice(int p_step) {
        ContactEvidence &seen = evidence();
        const netw::sim::Row *row = row_at(A, 0);
        if (row == nullptr) {
            return;
        }
        const bool now = !row->contact.touching.is_empty();
        if (now && !seen.was_touching) {
            seen.onsets += 1;
            if (seen.onsets == 1) {
                seen.first_onset_frame = p_step;
                seen.first_fence = row->contact.fence_tick;
            } else if (seen.onsets == 2) {
                seen.second_onset_frame = p_step;
            }
        }
        seen.was_touching = now;
        if (p_step == DOUBLE_BACK_FRAMES) {
            sphere_at(A, 1)->set_linear_velocity(Vector3(-6.0, 0.0, 0.0));
        }
        if (p_step == DOUBLE_AGAIN_FRAMES) {
            const Vector3 at = sphere_at(A, 0)->get_position();
            sphere_at(A, 1)->set_position(at - Vector3(0.98, 0.0, 0.0));
            sphere_at(A, 1)->set_linear_velocity(Vector3(6.0, 0.0, 0.0));
        }
        if (p_step == DOUBLE_READ_FRAMES) {
            seen.final_fence = row->contact.fence_tick;
            seen.fenced_total = row->installs.stats.fenced;
            sphere_at(A, 1)->set_linear_velocity(Vector3(-6.0, 0.0, 0.0));
        }
    }

    void drive(int p_step) {
        if (p_step == 0) {
            begin();
        }
        if (p_step >= 0) {
            observe(p_step);
        }
        if (p_step >= 0 && (kind == Kind::REST || kind == Kind::WAKE)) {
            hold_asleep(p_step);
        }
        if (kind == Kind::PUSH && p_step >= 0) {
            push(p_step);
        }
        if (kind == Kind::DOUBLE && p_step >= 0) {
            touch_twice(p_step);
        }
        if (kind == Kind::FENCE && p_step == FENCE_READ_FRAMES) {
            evidence().hit_velocity = sphere_at(A, 0)->get_linear_velocity();
            sphere_at(A, 1)->set_linear_velocity(Vector3(-6.0, 0.0, 0.0));
        }
    }

public:
    explicit ContactScenario(Kind p_kind) : kind(p_kind) {
    }

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
        drive(step);
        if (step >= SETTLE_FRAMES) {
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

class ChainScenario final : public ContactScenario {
public:
    ChainScenario() : ContactScenario(Kind::CHAIN) {
    }
};

class RefusedScenario final : public ContactScenario {
public:
    RefusedScenario() : ContactScenario(Kind::REFUSED) {
    }
};

class RestScenario final : public ContactScenario {
public:
    RestScenario() : ContactScenario(Kind::REST) {
    }
};

class WakeScenario final : public ContactScenario {
public:
    WakeScenario() : ContactScenario(Kind::WAKE) {
    }
};

class FenceScenario final : public ContactScenario {
public:
    FenceScenario() : ContactScenario(Kind::FENCE) {
    }
};

NETW_FRAME_SCENARIO(ChainScenario, contact_chain_scenario);
NETW_FRAME_SCENARIO(RefusedScenario, contact_refused_scenario);
NETW_FRAME_SCENARIO(RestScenario, contact_rest_scenario);
NETW_FRAME_SCENARIO(WakeScenario, contact_wake_scenario);
class PushScenario final : public ContactScenario {
public:
    PushScenario() : ContactScenario(Kind::PUSH) {
    }
};

NETW_FRAME_SCENARIO(FenceScenario, contact_fence_scenario);
class DoubleScenario final : public ContactScenario {
public:
    DoubleScenario() : ContactScenario(Kind::DOUBLE) {
    }
};

NETW_FRAME_SCENARIO(PushScenario, contact_push_scenario);
NETW_FRAME_SCENARIO(DoubleScenario, contact_double_scenario);

class AuthoredScenario final : public ContactScenario {
public:
    AuthoredScenario() : ContactScenario(Kind::AUTHORED) {
    }
};

NETW_FRAME_SCENARIO(AuthoredScenario, contact_authored_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] contact, a body a client spawned with "
    "claim_on_contact claims the free body it touches"
) {
    const ContactEvidence &evidence = evidence_of(Kind::AUTHORED);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_EQ(evidence.subjects[0].server_controller, evidence.holder);
    const Subject &touched = evidence.subjects[1];
    NETW_CHECK_GE(touched.claimed_frame, 0);
    NETW_CHECK_EQ(touched.server_controller, evidence.holder);
    NETW_CHECK_EQ(touched.server_hold, int64_t(Hold::HOLD_YIELDABLE));
}

TEST_CASE(
    "[Networked][Sim][Frame] a chain of free bodies touching a body this "
    "peer controls is claimed whole in one pass"
) {
    const ContactEvidence &evidence = evidence_of(Kind::CHAIN);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    const int first = evidence.subjects[1].claimed_frame;
    NETW_CHECK_GE(first, 0);
    for (int index = 1; index < 4; ++index) {
        NETW_FORMAT_INT(index_text, index);
        CAPTURE(index_text);
        const Subject &subject = evidence.subjects[index];
        NETW_CHECK_EQ(subject.claimed_frame, first);
        NETW_CHECK_EQ(subject.server_controller, evidence.holder);
        NETW_CHECK_EQ(subject.server_hold, int64_t(Hold::HOLD_YIELDABLE));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a refused grab takes nothing it touched while "
    "it ran ahead"
) {
    const ContactEvidence &evidence = evidence_of(Kind::REFUSED);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.subjects[1].ran_ahead);
    CHECK(evidence.grab_failed);
    NETW_CHECK_EQ(evidence.subjects[0].server_controller, evidence.rival);
    NETW_CHECK_EQ(evidence.subjects[1].server_controller, int64_t(0));
    NETW_CHECK_EQ(evidence.subjects[1].local_controller, int64_t(0));
}

TEST_CASE(
    "[Networked][Sim][Frame] a yieldable thawed body releases once it has "
    "slept the declared time under a confirmed claim"
) {
    const ContactEvidence &evidence = evidence_of(Kind::REST);
    REQUIRE(evidence.driven);
    const Subject &subject = evidence.subjects[0];
    REQUIRE(subject.granted_frame >= 0);
    REQUIRE(subject.releasing_frame >= 0);
    NETW_CHECK_GE(subject.releasing_frame - subject.granted_frame, REST_TICKS);
    NETW_CHECK_EQ(subject.server_controller, int64_t(0));
    NETW_CHECK_EQ(subject.local_controller, int64_t(0));
}

TEST_CASE(
    "[Networked][Sim][Frame] a frozen, exclusive or pending body that "
    "reports asleep never releases itself"
) {
    const ContactEvidence &evidence = evidence_of(Kind::REST);
    REQUIRE(evidence.driven);
    for (int index = 1; index < CUBES; ++index) {
        NETW_FORMAT_INT(index_text, index);
        CAPTURE(index_text);
        const Subject &subject = evidence.subjects[index];
        REQUIRE(subject.granted_frame >= 0);
        CHECK(subject.asleep_seen);
        NETW_CHECK_EQ(subject.releasing_frame, -1);
        NETW_CHECK_EQ(subject.server_controller, evidence.holder);
        NETW_CHECK_EQ(subject.server_hold, int64_t(REST_FINAL_HOLDS[index]));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a body that wakes before its rest release is "
    "decided claims itself again and keeps moving"
) {
    const ContactEvidence &evidence = evidence_of(Kind::WAKE);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.woke);
    const Subject &subject = evidence.subjects[0];
    NETW_CHECK_EQ(evidence.reclaims, int64_t(1));
    NETW_CHECK_EQ(subject.server_controller, evidence.holder);
    NETW_CHECK_EQ(subject.local_controller, evidence.holder);
    NETW_CHECK_EQ(subject.server_hold, int64_t(Hold::HOLD_YIELDABLE));
    CHECK(subject.local_velocity.is_equal_approx(WAKE_VELOCITY));
}

TEST_CASE(
    "[Networked][Sim][Frame] an active copy hit by a body this peer runs "
    "keeps the hit while its moving author streams, and the author's later "
    "samples correct it"
) {
    const ContactEvidence &evidence = evidence_of(Kind::FENCE);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.hit_velocity.x, 1.0);
    const Subject &ball = evidence.subjects[0];
    NETW_CHECK_EQ(ball.local_controller, int64_t(0));
    NETW_CHECK_CLOSE(ball.server_velocity.z, DRIFT.z, 0.01);
    NETW_CHECK_CLOSE(ball.local_velocity.x, 0.0, 0.05);
    NETW_CHECK_CLOSE(ball.local_position.x, ball.server_position.x, 0.05);
}

TEST_CASE(
    "[Networked][Sim][Frame] an active copy held against a body this peer "
    "runs is fenced once at the touch, and the author's later samples "
    "correct it while the push goes on"
) {
    const ContactEvidence &evidence = evidence_of(Kind::PUSH);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    CHECK(evidence.touching_at_read);
    NETW_CHECK_GT(evidence.fenced_in_contact, int64_t(0));
    NETW_CHECK_GT(evidence.installed_in_contact, int64_t(FLIGHT_TICKS));
}

TEST_CASE(
    "[Networked][Sim][Frame] a second touch before the author's samples from "
    "the first have landed keeps the first fence, so the author's own "
    "response to the first touch still installs"
) {
    const ContactEvidence &evidence = evidence_of(Kind::DOUBLE);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GE(evidence.onsets, 2);
    NETW_CHECK_LT(
        evidence.second_onset_frame - evidence.first_onset_frame,
        DOUBLE_FLIGHT_TICKS
    );
    NETW_CHECK_GE(evidence.first_fence, int64_t(0));
    NETW_CHECK_EQ(evidence.final_fence, evidence.first_fence);
    NETW_CHECK_LE(evidence.fenced_total, int64_t(DOUBLE_FLIGHT_TICKS + 1));
}

} // namespace TestNetwSimContactFrameLaws

#endif
