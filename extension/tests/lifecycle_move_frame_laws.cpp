#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/spawn/pipeline.hpp"

using namespace godot;

namespace TestLifecycleMoveFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr int TICKRATE = 30;
constexpr int HOLDER = 0;
constexpr int OBSERVER = 1;
constexpr int SEAT_FRAMES = 12;
constexpr int MOVE_FRAMES = 40;
constexpr int CARRY_TICKS = 120;
constexpr double QUANTUM = 1e-3;
constexpr const char *AVATAR_ID = "lifecycle_hold_avatar";
constexpr const char *CUBE_ID = "lifecycle_hold_cube";

constexpr const char *AVATAR_SOURCE = R"(extends Node3D

func _init() -> void:
	var hand := Node3D.new()
	hand.name = &"Hand"
	hand.position = Vector3(0.6, 1.0, -0.3)
	add_child(hand)
	Netw.configure_property(self, &"position").broadcast()
	Netw.configure_property(self, &"rotation").broadcast()
)";

constexpr const char *CUBE_SOURCE = R"(extends CharacterBody3D

func _init() -> void:
	collision_layer = 1
	collision_mask = 0
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(0.3, 0.3, 0.3)
	shape.shape = box
	add_child(shape)
	Netw.configure_property(self, &"position").broadcast()
)";

Script *avatar_script = nullptr;
Script *cube_script = nullptr;

Node *build_avatar(const Variant &p_name) {
    if (avatar_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(avatar_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_cube(const Variant &p_name) {
    if (cube_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(cube_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
        NetwEntity::ensure(made)->set_lifecycle(
            NetwEntity::LIFECYCLE_CONTROLLER
        );
    }
    return made;
}

struct HoldEvidence {
    bool driven = false;
    bool seated = false;
    bool resolved = false;
    bool session_holds = false;
    bool observer_holds = false;
    int64_t anchors[3] = {-1, -1, -1};
    int samples = 0;
    double drift = -1.0;
    double spin = 0.0;
};

HoldEvidence &evidence() {
    static HoldEvidence held;
    return held;
}

class HandStage : public netw_test::FrameScenario {
protected:
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> avatar_source;
    Ref<Script> cube_source;
    Ref<netw::NetwPromise> moved;
    int avatar = 0;
    int cube = 0;

    Node *hand_at(int p_client) const {
        Node *body = stand->node_at(p_client, avatar);
        return body != nullptr ? body->get_node_or_null(NodePath("Hand"))
                               : nullptr;
    }

    int spawn(const char *p_id, const char *p_name, Node *p_parent) {
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(p_name));
        const RID made
            = server->spawn_registered(StringName(p_id), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return 0;
        }
        p_parent->add_child(built);
        stand->pump(4);
        return int(server->entity_get_route(made));
    }

    bool open() {
        avatar_source = netw_test::minted_script(AVATAR_SOURCE);
        cube_source = netw_test::minted_script(CUBE_SOURCE);
        if (avatar_source.is_null() || cube_source.is_null()) {
            return false;
        }
        avatar_script = avatar_source.ptr();
        cube_script = cube_source.ptr();
        stand = new netw_test::SimStand(2);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(AVATAR_ID), callable_mp_static(&build_avatar));
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        avatar = spawn(AVATAR_ID, "Avatar", arena);
        cube = spawn(CUBE_ID, "Cube", arena);
        stand->pump(8);
        if (avatar == 0 || cube == 0) {
            return false;
        }
        const int64_t holder = stand->peer_id(HOLDER);
        NetwEntity::of(stand->node_at(-1, avatar))->set_controller(holder);
        NetwEntity::of(stand->node_at(-1, cube))->set_controller(holder);
        Node3D *placed = Object::cast_to<Node3D>(stand->node_at(HOLDER, cube));
        if (placed != nullptr) {
            placed->set_position(Vector3(2.5, 1.0, 0.5));
        }
        return true;
    }

    void move() {
        NetwMultiplayer *holder = stand->session(HOLDER);
        moved = holder->entity_reparent(
            holder->entity_from_route(cube),
            hand_at(HOLDER)
        );
    }

    void close_stage() {
        moved = Ref<netw::NetwPromise>();
        delete stand;
        stand = nullptr;
        avatar_script = nullptr;
        cube_script = nullptr;
        avatar_source = Ref<Script>();
        cube_source = Ref<Script>();
    }
};

class HoldScenario final : public HandStage {
    Vector3 first_offset;
    Basis first_turn;

    void carry(int p_tick) {
        Node3D *body = Object::cast_to<Node3D>(stand->node_at(HOLDER, avatar));
        if (body != nullptr) {
            body->set_rotation(Vector3(0.0, real_t(0.06 * p_tick), 0.0));
            body->set_position(
                Vector3(real_t(0.04 * p_tick), 0.0, real_t(0.02 * p_tick))
            );
        }
        Node3D *hand = Object::cast_to<Node3D>(hand_at(OBSERVER));
        Node3D *held = Object::cast_to<Node3D>(stand->node_at(OBSERVER, cube));
        if (hand == nullptr || held == nullptr || !held->is_inside_tree()) {
            return;
        }
        const Transform3D offset = hand->get_global_transform().affine_inverse()
            * held->get_global_transform();
        HoldEvidence &seen = evidence();
        if (seen.samples == 0) {
            first_offset = offset.origin;
            first_turn = offset.basis;
            seen.drift = 0.0;
        }
        seen.samples += 1;
        seen.drift
            = MAX(seen.drift, double(offset.origin.distance_to(first_offset)));
        for (int axis = 0; axis < 3; ++axis) {
            seen.drift = MAX(
                seen.drift,
                double((offset.basis.get_column(axis)
                        - first_turn.get_column(axis))
                           .length())
            );
        }
        seen.spin = double(hand->get_global_transform().basis.get_euler().y);
    }

    void close() {
        HoldEvidence &seen = evidence();
        seen.driven = true;
        seen.resolved = moved.is_valid() && moved->get_is_settled()
            && !moved->get_is_failed();
        Node *session_cube = stand->node_at(-1, cube);
        Node *observer_cube = stand->node_at(OBSERVER, cube);
        seen.session_holds = session_cube != nullptr
            && session_cube->get_parent() == hand_at(-1);
        seen.observer_holds = observer_cube != nullptr
            && observer_cube->get_parent() == hand_at(OBSERVER);
        for (int client = -1; client < 2; ++client) {
            seen.anchors[client + 1]
                = int64_t(stand->session(client)->liveness_route_anchor(cube));
        }
        close_stage();
    }

public:
    bool advance() override {
        if (done || netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                evidence().driven = true;
                done = true;
                return false;
            }
            ++frame;
            return true;
        }
        if (frame < SEAT_FRAMES) {
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        if (frame == SEAT_FRAMES) {
            evidence().seated = stand->node_at(OBSERVER, cube) != nullptr;
            move();
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        if (frame < SEAT_FRAMES + MOVE_FRAMES) {
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        const int tick = frame - SEAT_FRAMES - MOVE_FRAMES;
        if (tick < CARRY_TICKS) {
            carry(tick);
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        close();
        done = true;
        return false;
    }
};

NETW_FRAME_SCENARIO(HoldScenario, lifecycle_hold_scenario);

struct LostCarryEvidence {
    bool driven = false;
    bool admitted = false;
    bool hand_freed = false;
    bool settled = false;
    bool failed = false;
    int64_t code = -1;
    bool holder_holds_ops = true;
    bool holder_at_arena = false;
    bool session_at_arena = false;
    int64_t anchors[2] = {-1, -1};
    int64_t authors[2] = {-1, -1};
};

LostCarryEvidence &lost_evidence() {
    static LostCarryEvidence held;
    return held;
}

class LostCarryScenario final : public HandStage {
    int64_t admitted_before = 0;
    Node *session_origin = nullptr;
    Node *holder_origin = nullptr;

    int64_t admitted() const {
        const Dictionary counted = stand->session(-1)
                                       ->get_replication_plane()
                                       ->get_spawn_pipeline()
                                       ->counters();
        return int64_t(counted.get(StringName("lifecycle_ops_admitted"), 0));
    }

    void free_session_hand() {
        Node *hand = hand_at(-1);
        if (hand == nullptr) {
            return;
        }
        hand->get_parent()->remove_child(hand);
        memdelete(hand);
        lost_evidence().hand_freed = true;
    }

    void close() {
        LostCarryEvidence &seen = lost_evidence();
        seen.driven = true;
        seen.settled = moved.is_valid() && moved->get_is_settled();
        seen.failed = moved.is_valid() && moved->get_is_failed();
        seen.code = moved.is_valid() ? int64_t(moved->get_code()) : -1;
        Node *held = stand->node_at(HOLDER, cube);
        const Ref<NetwEntity> holder_entity
            = held != nullptr ? NetwEntity::of(held) : Ref<NetwEntity>();
        seen.holder_holds_ops
            = holder_entity.is_null() || holder_entity->has_structure_ops();
        seen.holder_at_arena = held != nullptr && holder_origin != nullptr
            && held->get_parent() == holder_origin;
        Node *session_cube = stand->node_at(-1, cube);
        seen.session_at_arena = session_cube != nullptr
            && session_origin != nullptr
            && session_cube->get_parent() == session_origin;
        const int sides[2] = {-1, HOLDER};
        for (int at = 0; at < 2; ++at) {
            const NetwMultiplayer::AnchorRevision installed
                = stand->session(sides[at])->anchor_installed(cube);
            seen.anchors[at] = int64_t(installed.revision);
            seen.authors[at] = int64_t(installed.author);
        }
        close_stage();
    }

public:
    bool advance() override {
        if (done || netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                lost_evidence().driven = true;
                done = true;
                return false;
            }
            ++frame;
            return true;
        }
        if (frame < SEAT_FRAMES) {
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        if (frame == SEAT_FRAMES) {
            admitted_before = admitted();
            Node *session_cube = stand->node_at(-1, cube);
            Node *holder_cube = stand->node_at(HOLDER, cube);
            session_origin = session_cube != nullptr
                ? session_cube->get_parent()
                : nullptr;
            holder_origin
                = holder_cube != nullptr ? holder_cube->get_parent() : nullptr;
            move();
            stand->step_ticks(1);
            ++frame;
            return true;
        }
        if (frame < SEAT_FRAMES + MOVE_FRAMES) {
            stand->step_ticks(1);
            if (!lost_evidence().admitted && admitted() > admitted_before) {
                lost_evidence().admitted = true;
                free_session_hand();
            }
            ++frame;
            return true;
        }
        close();
        done = true;
        return false;
    }
};

NETW_FRAME_SCENARIO(LostCarryScenario, lifecycle_lost_carry_scenario);

} // namespace TestLifecycleMoveFrameLaws

TEST_CASE(
    "[Networked][Lifecycle][Spawn][Frame] HC1 on an observer the hand to cube "
    "offset stays constant within quantization while the holder spins and "
    "strafes, after the holder moved its cube into its hand"
) {
    const TestLifecycleMoveFrameLaws::HoldEvidence &seen
        = TestLifecycleMoveFrameLaws::evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    CHECK(seen.resolved);
    CHECK(seen.session_holds);
    CHECK(seen.observer_holds);
    NETW_CHECK_EQ(seen.anchors[0], int64_t(2));
    NETW_CHECK_EQ(seen.anchors[1], int64_t(2));
    NETW_CHECK_EQ(seen.anchors[2], int64_t(2));
    NETW_CHECK_GT(seen.samples, 100);
    NETW_CHECK_GE(seen.drift, 0.0);
    NETW_CHECK_LT(seen.drift, TestLifecycleMoveFrameLaws::QUANTUM);
    NETW_CHECK_GT(seen.spin * seen.spin, 0.01);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][Frame] lost carry, an admitted move whose "
    "destination is freed on the session before the carry places the node is "
    "refused to its author, who moves back to the session's anchor"
) {
    const TestLifecycleMoveFrameLaws::LostCarryEvidence &seen
        = TestLifecycleMoveFrameLaws::lost_evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.admitted);
    REQUIRE(seen.hand_freed);
    CHECK(seen.settled);
    CHECK(seen.failed);
    NETW_CHECK_EQ(seen.code, int64_t(ERR_INVALID_PARAMETER));
    CHECK_FALSE(seen.holder_holds_ops);
    CHECK(seen.session_at_arena);
    CHECK(seen.holder_at_arena);
    NETW_CHECK_EQ(seen.anchors[1], seen.anchors[0]);
    NETW_CHECK_EQ(seen.authors[1], seen.authors[0]);
}

#endif
