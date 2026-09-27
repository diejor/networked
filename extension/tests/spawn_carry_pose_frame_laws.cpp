#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

#include "godot/callable.hpp"
#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"

using namespace godot;
using netw::NetwMultiplayer;

namespace NetwTests {

namespace {

constexpr double POSE_TOLERANCE = 1e-4;
constexpr int SETTLE_FRAMES = 20;
constexpr int CARRY_FRAMES = 16;
const Vector3 DESTINATION_OFFSET = Vector3(1.5, 1.0, 0.0);
const Vector3 SLIDE_VELOCITY = Vector3(3.0, 0.0, 0.0);
const Vector3 RESTING_START = Vector3(0.0, 0.5, 0.0);
const Vector3 AIRBORNE_START = Vector3(0.0, 5.0, 0.0);

enum Kind {
    KIND_NODE_3D,
    KIND_RIGID_RESTING,
    KIND_RIGID_SLIDING,
    KIND_RIGID_FROZEN,
    KIND_CHARACTER_3D,
    KIND_AREA_3D,
    KIND_NODE_2D,
    KIND_COUNT,
};

const char *KIND_LABELS[KIND_COUNT] = {
    "Node3D",
    "RigidBody3D resting",
    "RigidBody3D sliding",
    "RigidBody3D frozen",
    "CharacterBody3D",
    "Area3D",
    "Node2D",
};

struct PoseEvidence {
    bool driven = false;
    bool landed = false;
    double origin_error = -1.0;
    double basis_error = -1.0;
    Vector3 velocity_before;
    Vector3 velocity_after;
};

struct CarryPoseEvidence {
    PoseEvidence kinds[KIND_COUNT];
};

CarryPoseEvidence &carry_pose_evidence() {
    static CarryPoseEvidence evidence;
    return evidence;
}

struct Pose {
    Vector3 origin;
    Vector3 columns[3];
};

Pose pose_of(Node *p_node) {
    Pose pose;
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        const Transform3D global = spatial->get_global_transform();
        pose.origin = global.origin;
        for (int axis = 0; axis < 3; ++axis) {
            pose.columns[axis] = global.basis.get_column(axis);
        }
    } else if (Node2D *flat = Object::cast_to<Node2D>(p_node)) {
        const Transform2D global = flat->get_global_transform();
        pose.origin = Vector3(global.get_origin().x, global.get_origin().y, 0);
        pose.columns[0] = Vector3(global.columns[0].x, global.columns[0].y, 0);
        pose.columns[1] = Vector3(global.columns[1].x, global.columns[1].y, 0);
    }
    return pose;
}

double basis_distance(const Pose &p_a, const Pose &p_b) {
    double worst = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        worst = MAX(
            worst,
            double((p_a.columns[axis] - p_b.columns[axis]).length())
        );
    }
    return worst;
}

Vector3 velocity_of(Node *p_node) {
    if (RigidBody3D *rigid = Object::cast_to<RigidBody3D>(p_node)) {
        return rigid->get_linear_velocity();
    }
    return Vector3();
}

CollisionShape3D *sphere_shape() {
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<SphereShape3D> sphere;
    sphere.instantiate();
    sphere->set_radius(0.5);
    shape->set_shape(sphere);
    return shape;
}

Node3D *seat_floor(Node *p_stage) {
    StaticBody3D *floor_body = memnew(StaticBody3D);
    floor_body->set_position(Vector3(0.0, -0.5, 0.0));
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(40.0, 1.0, 40.0));
    shape->set_shape(box);
    floor_body->add_child(shape);
    p_stage->add_child(floor_body);
    return floor_body;
}

Node *seat_mover(Kind p_kind, Node *p_source) {
    Node *mover = nullptr;
    switch (p_kind) {
        case KIND_NODE_3D: {
            Node3D *plain = memnew(Node3D);
            plain->set_position(RESTING_START);
            mover = plain;
        } break;
        case KIND_RIGID_RESTING: {
            RigidBody3D *rigid = memnew(RigidBody3D);
            rigid->add_child(sphere_shape());
            rigid->set_position(RESTING_START);
            mover = rigid;
        } break;
        case KIND_RIGID_SLIDING: {
            RigidBody3D *rigid = memnew(RigidBody3D);
            rigid->add_child(sphere_shape());
            rigid->set_gravity_scale(0.0);
            rigid->set_linear_damp_mode(RigidBody3D::DAMP_MODE_REPLACE);
            rigid->set_linear_damp(0.0);
            rigid->set_position(AIRBORNE_START);
            rigid->set_linear_velocity(SLIDE_VELOCITY);
            mover = rigid;
        } break;
        case KIND_RIGID_FROZEN: {
            RigidBody3D *rigid = memnew(RigidBody3D);
            rigid->add_child(sphere_shape());
            rigid->set_freeze_enabled(true);
            rigid->set_position(AIRBORNE_START);
            mover = rigid;
        } break;
        case KIND_CHARACTER_3D: {
            CharacterBody3D *character = memnew(CharacterBody3D);
            character->add_child(sphere_shape());
            character->set_position(RESTING_START);
            mover = character;
        } break;
        case KIND_AREA_3D: {
            Area3D *area = memnew(Area3D);
            area->add_child(sphere_shape());
            area->set_position(RESTING_START);
            mover = area;
        } break;
        case KIND_NODE_2D: {
            Node2D *flat = memnew(Node2D);
            flat->set_position(Vector2(0.0, 0.5));
            mover = flat;
        } break;
        default:
            break;
    }
    mover->set_name("Mover");
    p_source->add_child(mover);
    return mover;
}

struct Landing {
    Node *mover = nullptr;
    PoseEvidence *seen = nullptr;
    Pose before;
};

Landing &landing() {
    static Landing watched;
    return watched;
}

void note_landing(RID) {
    Landing &watched = landing();
    if (watched.mover == nullptr || watched.seen == nullptr
        || watched.seen->landed) {
        return;
    }
    PoseEvidence &seen = *watched.seen;
    const Pose after = pose_of(watched.mover);
    seen.landed = true;
    seen.origin_error = double((after.origin - watched.before.origin).length());
    seen.basis_error = basis_distance(after, watched.before);
    seen.velocity_after = velocity_of(watched.mover);
}

class CarryPoseScenario final : public netw_test::FrameScenario {
    int kind = 0;
    int step = 0;
    Ref<NetwMultiplayer> core;
    Node *stage = nullptr;
    Node *destination = nullptr;
    Node *mover = nullptr;

    void open() {
        core.instantiate();
        if (kind == KIND_NODE_2D) {
            Node2D *flat_stage = memnew(Node2D);
            Node2D *source = memnew(Node2D);
            source->set_name("Source");
            Node2D *target = memnew(Node2D);
            target->set_name("Destination");
            target->set_position(
                Vector2(DESTINATION_OFFSET.x, DESTINATION_OFFSET.y)
            );
            target->set_rotation(real_t(Math_PI * 0.5));
            flat_stage->add_child(source);
            flat_stage->add_child(target);
            stage = flat_stage;
            destination = target;
            netw::gd::scene_root()->add_child(stage);
            mover = seat_mover(Kind(kind), source);
            return;
        }
        Node3D *spatial_stage = memnew(Node3D);
        Node3D *source = memnew(Node3D);
        source->set_name("Source");
        Node3D *target = memnew(Node3D);
        target->set_name("Destination");
        target->set_position(DESTINATION_OFFSET);
        target->set_rotation(Vector3(0.0, real_t(Math_PI * 0.5), 0.0));
        spatial_stage->add_child(source);
        spatial_stage->add_child(target);
        seat_floor(spatial_stage);
        stage = spatial_stage;
        destination = target;
        netw::gd::scene_root()->add_child(stage);
        mover = seat_mover(Kind(kind), source);
    }

    void close() {
        carry_pose_evidence().kinds[kind].driven = true;
        landing() = Landing();
        netw::gd::scene_root()->remove_child(stage);
        memdelete(stage);
        stage = nullptr;
        destination = nullptr;
        mover = nullptr;
        core.unref();
        step = 0;
        ++kind;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || kind >= KIND_COUNT) {
            return false;
        }
        PoseEvidence &seen = carry_pose_evidence().kinds[kind];
        if (step == 0) {
            open();
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES) {
            ++step;
            return true;
        }
        if (step == SETTLE_FRAMES + 1) {
            Landing &watched = landing();
            watched.mover = mover;
            watched.seen = &seen;
            watched.before = pose_of(mover);
            seen.velocity_before = velocity_of(mover);
            core->spawn_reparent_node(
                mover,
                destination,
                callable_mp_static(&note_landing)
            );
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES + 1 + CARRY_FRAMES) {
            ++step;
            return true;
        }
        close();
        return kind < KIND_COUNT;
    }
};

NETW_FRAME_SCENARIO(CarryPoseScenario, carry_pose_scenario);

} // namespace

TEST_CASE(
    "[Networked][Spawn][Frame] a peer applying a reparent frame keeps the "
    "body's world pose, so it lands under the new parent where it stood "
    "under the old one, for every body kind"
) {
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        const PoseEvidence &seen = carry_pose_evidence().kinds[kind];
        NETW_FORMAT_TEXT(netw_kind_text, KIND_LABELS[kind]);
        CAPTURE(netw_kind_text);
        REQUIRE(seen.driven);
        CHECK(seen.landed);
        NETW_CHECK_LT(seen.origin_error, POSE_TOLERANCE);
        NETW_CHECK_LT(seen.basis_error, POSE_TOLERANCE);
        NETW_CHECK_GE(seen.origin_error, 0.0);
    }
}

TEST_CASE(
    "[Networked][Spawn][Frame] a peer applying a reparent frame keeps a "
    "moving body's velocity"
) {
    const PoseEvidence &seen = carry_pose_evidence().kinds[KIND_RIGID_SLIDING];
    REQUIRE(seen.driven);
    NETW_CHECK_GT(double(seen.velocity_before.x), 2.9);
    NETW_CHECK_LT(
        double((seen.velocity_after - seen.velocity_before).length()),
        POSE_TOLERANCE
    );
}

} // namespace NetwTests
