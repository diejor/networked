#include "support/frame_drive.h"
#include "support/netw_test.h"

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>

#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/wiring.hpp"

using namespace godot;

namespace TestNetwLagCompColliderFrameLaws {

namespace {

constexpr uint32_t HULL = 1;
constexpr uint32_t HITBOX = 2;
constexpr uint32_t GEAR = 4;
constexpr double PAST_X = 0.0;
constexpr double LIVE_X = 10.0;
constexpr double STRIDE = 0.5;
constexpr double GEAR_Z = 3.0;
constexpr double TARGET_Z = 0.0;
constexpr double MOVED_Z = 20.0;
constexpr double STILL_Z = 40.0;
constexpr double REPLAY_Z = 60.0;
constexpr int64_t PAST_TICK = 1;
constexpr int SETTLE_FRAMES = 3;

struct ColliderEvidence {
    bool driven = false;
    bool flushes = false;
    bool hitbox_at_past = false;
    bool hitbox_at_live = true;
    bool hull_at_past = false;
    bool hull_at_live = false;
    bool hitbox_after = false;
    bool hull_after = false;
    bool gear_at_past = false;
    double moved_speed = 0.0;
    double still_speed = 0.0;
    bool replay_hitbox_at_restored = false;
    bool replay_hull_at_restored = false;
};

ColliderEvidence &collider_evidence() {
    static ColliderEvidence evidence;
    return evidence;
}

PhysicsDirectSpaceState3D *space_of(Node3D *p_node) {
    return p_node->get_world_3d()->get_direct_space_state();
}

Node3D *probe_anchor = nullptr;

bool ray_hits(double p_x, double p_z, uint32_t p_mask) {
    const Vector3 at(real_t(p_x), 0.0, real_t(p_z));
    Ref<PhysicsRayQueryParameters3D> ray = PhysicsRayQueryParameters3D::create(
        at + Vector3(0.0, 5.0, 0.0),
        at - Vector3(0.0, 5.0, 0.0),
        p_mask
    );
    ray->set_collide_with_areas(true);
    return !space_of(probe_anchor)->intersect_ray(ray).is_empty();
}

bool shape_hits(double p_x, double p_z, uint32_t p_mask) {
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(0.2, 0.2, 0.2));
    Ref<PhysicsShapeQueryParameters3D> query;
    query.instantiate();
    query->set_shape(box);
    query->set_transform(
        Transform3D(Basis(), Vector3(real_t(p_x), 0.0, real_t(p_z)))
    );
    query->set_collision_mask(p_mask);
    query->set_collide_with_areas(true);
    return !space_of(probe_anchor)->intersect_shape(query).is_empty();
}

CollisionShape3D *cube() {
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    shape->set_shape(box);
    return shape;
}

Area3D *hitbox(uint32_t p_layer, const Vector3 &p_offset) {
    Area3D *area = memnew(Area3D);
    area->set_collision_layer(p_layer);
    area->set_collision_mask(0);
    area->set_position(p_offset);
    area->add_child(cube());
    return area;
}

CharacterBody3D *character(Node *p_parent, double p_z) {
    CharacterBody3D *body = memnew(CharacterBody3D);
    body->set_collision_layer(HULL);
    body->set_collision_mask(0);
    body->add_child(cube());
    body->add_child(hitbox(HITBOX, Vector3()));
    body->set_position(Vector3(real_t(LIVE_X), 0.0, real_t(p_z)));
    p_parent->add_child(body);
    return body;
}

double server_speed(CharacterBody3D *p_body) {
    return Vector3(
               PhysicsServer3D::get_singleton()->body_get_state(
                   p_body->get_rid(),
                   PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY
               )
    )
        .length();
}

Dictionary pose_row(double p_x, double p_z) {
    Dictionary row;
    row[StringName("position")] = Vector3(real_t(p_x), 0.0, real_t(p_z));
    return row;
}

void look_while_rewound() {
    ColliderEvidence &evidence = collider_evidence();
    evidence.hitbox_at_past = ray_hits(PAST_X, TARGET_Z, HITBOX);
    evidence.hitbox_at_live = ray_hits(LIVE_X, TARGET_Z, HITBOX);
    evidence.hull_at_past = ray_hits(PAST_X, TARGET_Z, HULL);
    evidence.hull_at_live = ray_hits(LIVE_X, TARGET_Z, HULL);
}

void look_for_gear() {
    collider_evidence().gear_at_past
        = ray_hits(PAST_X, TARGET_Z + GEAR_Z, GEAR);
}

LocalVector<netw::predict::FieldDecl> pose_fields() {
    LocalVector<netw::predict::FieldDecl> fields;
    netw::predict::FieldDecl position;
    position.key = StringName("position");
    position.type = int(Variant::VECTOR3);
    fields.push_back(position);
    return fields;
}

class ColliderScenario final : public netw_test::FrameScenario {
    int frame = 0;
    SubViewport *viewport = nullptr;
    CharacterBody3D *target = nullptr;
    CharacterBody3D *moved = nullptr;
    CharacterBody3D *still = nullptr;
    CharacterBody3D *replayed = nullptr;
    netw::NetwLagCompCore *history = nullptr;
    netw::NetwPredictionEngine *prediction = nullptr;
    int64_t target_slot = -1;
    int64_t moved_slot = -1;
    int64_t replay_slot = -1;

    int64_t timeline(Node *p_owner, double p_z) {
        const int64_t slot = history->timeline_open(64);
        history->timeline_bind_owner(slot, p_owner);
        Array keys;
        keys.push_back(StringName("position"));
        history->timeline_declare(slot, keys);
        history->timeline_record(slot, PAST_TICK, pose_row(PAST_X, p_z));
        return slot;
    }

    void open() {
        viewport = memnew(SubViewport);
        viewport->set_use_own_world_3d(true);
        Ref<World3D> own;
        own.instantiate();
        viewport->set_world_3d(own);
        netw::gd::scene_root()->add_child(viewport);
        Node3D *arena = memnew(Node3D);
        viewport->add_child(arena);
        probe_anchor = arena;

        target = character(arena, TARGET_Z);
        moved = character(arena, MOVED_Z);
        still = character(arena, STILL_Z);
        replayed = character(arena, REPLAY_Z);

        history = new netw::NetwLagCompCore();
        target_slot = timeline(target, TARGET_Z);
        moved_slot = timeline(moved, MOVED_Z);

        prediction = new netw::NetwPredictionEngine();
        const LocalVector<netw::predict::FieldDecl> fields = pose_fields();
        replay_slot = prediction->open(fields);
        prediction->rewire(replay_slot, fields, {});
        prediction->bind_owner(replay_slot, replayed);
        replayed->set_position(Vector3(real_t(PAST_X), 0.0, real_t(REPLAY_Z)));
        prediction->capture_predicted(replay_slot, PAST_TICK);
        replayed->set_position(Vector3(real_t(LIVE_X), 0.0, real_t(REPLAY_Z)));
    }

    void rewind_target() {
        PackedInt64Array slots;
        slots.push_back(target_slot);
        history
            ->rewind(slots, PAST_TICK, callable_mp_static(&look_while_rewound));
        ColliderEvidence &evidence = collider_evidence();
        evidence.hitbox_after = ray_hits(LIVE_X, TARGET_Z, HITBOX);
        evidence.hull_after = ray_hits(LIVE_X, TARGET_Z, HULL);
    }

    void stride_and_rewind() {
        for (CharacterBody3D *body : {moved, still}) {
            body->set_position(
                body->get_position() + Vector3(real_t(STRIDE), 0.0, 0.0)
            );
            body->force_update_transform();
        }
        PackedInt64Array slots;
        slots.push_back(moved_slot);
        history->rewind(slots, PAST_TICK, Callable());
    }

    void attach_gear() {
        target->add_child(hitbox(GEAR, Vector3(0.0, 0.0, real_t(GEAR_Z))));
    }

    void rewind_for_gear() {
        PackedInt64Array slots;
        slots.push_back(target_slot);
        history->rewind(slots, PAST_TICK, callable_mp_static(&look_for_gear));
    }

    void replay_tick_one() {
        prediction->restore_predicted(replay_slot, PAST_TICK);
        ColliderEvidence &evidence = collider_evidence();
        evidence.replay_hitbox_at_restored
            = shape_hits(PAST_X, REPLAY_Z, HITBOX);
        evidence.replay_hull_at_restored = shape_hits(PAST_X, REPLAY_Z, HULL);
    }

    void close() {
        ColliderEvidence &evidence = collider_evidence();
        evidence.driven = true;
        evidence.flushes = netw::gd::can_flush_kinematic();
        delete prediction;
        prediction = nullptr;
        delete history;
        history = nullptr;
        probe_anchor = nullptr;
        netw::gd::scene_root()->remove_child(viewport);
        memdelete(viewport);
        viewport = nullptr;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        const int at = frame++;
        if (at == 0) {
            open();
        } else if (at == SETTLE_FRAMES) {
            rewind_target();
            stride_and_rewind();
        } else if (at == SETTLE_FRAMES + 1) {
            ColliderEvidence &evidence = collider_evidence();
            evidence.moved_speed = server_speed(moved);
            evidence.still_speed = server_speed(still);
            attach_gear();
        } else if (at == SETTLE_FRAMES + 2) {
            rewind_for_gear();
            replay_tick_one();
        } else if (at > SETTLE_FRAMES + 2) {
            close();
            frame = -1;
            return false;
        }
        return true;
    }
};

NETW_FRAME_SCENARIO(ColliderScenario, collider_scenario);

} // namespace

TEST_CASE(
    "[Networked][LagComp][Frame] K-RW1 a ray inside a rewind hits an Area "
    "hitbox below the entity root at the rewound pose and not the live one"
) {
    const ColliderEvidence &evidence = collider_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.hitbox_at_past);
    CHECK_FALSE(evidence.hitbox_at_live);
    CHECK(evidence.hitbox_after);
}

TEST_CASE(
    "[Networked][LagComp][Frame] K-RW2 a ray inside a rewind sees a kinematic "
    "root at the rewound pose on a build that flushes, and at its live pose "
    "on one that does not"
) {
    const ColliderEvidence &evidence = collider_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.hull_at_past == evidence.flushes);
    CHECK(evidence.hull_at_live != evidence.flushes);
    CHECK(evidence.hull_after);
}

TEST_CASE(
    "[Networked][LagComp][Frame] K-RW3 a character that moved this tick and "
    "was rewound keeps the velocity the step gives it without the rewind"
) {
    const ColliderEvidence &evidence = collider_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.still_speed, 1.0);
    NETW_CHECK_CLOSE(evidence.moved_speed, evidence.still_speed, 0.001);
}

TEST_CASE(
    "[Networked][LagComp][Frame] K-CL1 a hitbox attached after the first "
    "rewind moves with the next one"
) {
    const ColliderEvidence &evidence = collider_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.gear_at_past);
}

TEST_CASE(
    "[Networked][Predict][Frame] K-TR1 a shape query on replay tick one sees "
    "the hitbox at the restored pose, and the kinematic root there on a "
    "build that flushes"
) {
    const ColliderEvidence &evidence = collider_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.replay_hitbox_at_restored);
    CHECK(evidence.replay_hull_at_restored == evidence.flushes);
}

} // namespace TestNetwLagCompColliderFrameLaws
