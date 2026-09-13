#include "support/frame_drive.h"
#include "support/netw_test.h"

#include <cmath>

#include "godot/collision_shape.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr double BALL_RADIUS = 0.5;
constexpr double BALL_MASS = 1.0;
constexpr double GRAVITY_SCALE = 5.0;
constexpr double LINEAR_DAMP = 1.5;
constexpr double ANGULAR_DAMP = 4.0;
constexpr double FRICTION = 1.0;
constexpr double GROUND_EXTENT = 200.0;
constexpr double GROUND_THICKNESS = 1.0;

constexpr int GATE_SOLVES = 120;
constexpr int GATE_FRAME_CEILING = GATE_SOLVES * 4;
constexpr double GATE_EPSILON = 0.000001;

struct GateWorld {
    SubViewport *viewport = nullptr;
    RigidBody3D *body = nullptr;
};

RID stepping_space(const GateWorld &p_world) {
    if (p_world.body == nullptr) {
        return RID();
    }
    return p_world.body->get_world_3d()->get_space();
}

Ref<PhysicsMaterial> rough_material() {
    Ref<PhysicsMaterial> material;
    material.instantiate();
    material->set_friction(real_t(FRICTION));
    material->set_rough(true);
    return material;
}

GateWorld seat_world() {
    GateWorld world;
    world.viewport = memnew(SubViewport);
    world.viewport->set_use_own_world_3d(true);
    Ref<World3D> own;
    own.instantiate();
    world.viewport->set_world_3d(own);
    netw::gd::scene_root()->add_child(world.viewport);

    StaticBody3D *ground = memnew(StaticBody3D);
    CollisionShape3D *ground_shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(
        real_t(GROUND_EXTENT),
        real_t(GROUND_THICKNESS),
        real_t(GROUND_EXTENT)
    ));
    ground_shape->set_shape(box);
    ground->add_child(ground_shape);
    ground->set_position(Vector3(0.0, real_t(-GROUND_THICKNESS * 0.5), 0.0));
    ground->set_physics_material_override(rough_material());
    world.viewport->add_child(ground);

    world.body = memnew(RigidBody3D);
    CollisionShape3D *ball_shape = memnew(CollisionShape3D);
    Ref<SphereShape3D> ball;
    ball.instantiate();
    ball->set_radius(real_t(BALL_RADIUS));
    ball_shape->set_shape(ball);
    world.body->add_child(ball_shape);
    world.body->set_mass(real_t(BALL_MASS));
    world.body->set_gravity_scale(real_t(GRAVITY_SCALE));
    world.body->set_linear_damp(real_t(LINEAR_DAMP));
    world.body->set_angular_damp_mode(RigidBody3D::DAMP_MODE_REPLACE);
    world.body->set_angular_damp(real_t(ANGULAR_DAMP));
    world.body->set_physics_material_override(rough_material());
    world.body->set_position(Vector3(0.0, real_t(BALL_RADIUS), 0.0));
    world.viewport->add_child(world.body);
    return world;
}

void release_world(GateWorld &p_world) {
    if (p_world.viewport == nullptr) {
        return;
    }
    netw::gd::scene_root()->remove_child(p_world.viewport);
    memdelete(p_world.viewport);
    p_world.viewport = nullptr;
    p_world.body = nullptr;
}

void hold_world(const GateWorld &p_world, bool p_held) {
    PhysicsServer3D::get_singleton()->space_set_active(
        stepping_space(p_world),
        !p_held
    );
}

double physics_delta() {
    return 1.0
        / double(Engine::get_singleton()->get_physics_ticks_per_second());
}

Vector3 gate_drive(int p_solve, double p_delta) {
    const double turn = std::sin(double(p_solve) * 0.05);
    return Vector3(real_t(turn), 0.0, 1.0).normalized()
        * real_t(60.0 * p_delta);
}

struct HoldSkipEvidence {
    bool driven = false;
    int solves = 0;
    int frames = 0;
    double position_gap = 0.0;
    double linear_gap = 0.0;
    double angular_gap = 0.0;
};

HoldSkipEvidence &hold_skip_evidence(int p_index) {
    static HoldSkipEvidence evidence[2];
    return evidence[p_index];
}

class HoldSkipScenario final : public netw_test::FrameScenario {
    const int hold_period;
    const int slot;
    bool seated = false;
    int settle = 0;
    bool pending = false;
    bool pending_open = false;
    bool pending_gated = false;
    int open_solves = 0;
    int gated_solves = 0;
    int frames = 0;
    GateWorld open;
    GateWorld gated;

public:
    HoldSkipScenario(int p_hold_period, int p_slot)
        : hold_period(p_hold_period), slot(p_slot) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (!seated) {
            open = seat_world();
            gated = seat_world();
            seated = true;
            settle = 2;
            return true;
        }
        if (settle > 0) {
            --settle;
            return true;
        }
        if (pending) {
            open_solves += pending_open ? 1 : 0;
            gated_solves += pending_gated ? 1 : 0;
            ++frames;
            pending = false;
        }
        if (gated_solves < GATE_SOLVES && frames < GATE_FRAME_CEILING) {
            const double delta = physics_delta();
            pending_open = open_solves < GATE_SOLVES;
            pending_gated
                = gated_solves < GATE_SOLVES && frames % hold_period != 0;
            if (pending_open) {
                open.body->set_angular_velocity(
                    open.body->get_angular_velocity()
                    + gate_drive(open_solves, delta)
                );
            }
            if (pending_gated) {
                gated.body->set_angular_velocity(
                    gated.body->get_angular_velocity()
                    + gate_drive(gated_solves, delta)
                );
            }
            hold_world(open, !pending_open);
            hold_world(gated, !pending_gated);
            pending = true;
            return true;
        }
        hold_world(open, false);
        hold_world(gated, false);

        HoldSkipEvidence &evidence = hold_skip_evidence(slot);
        evidence.driven = true;
        evidence.solves = gated_solves;
        evidence.frames = frames;
        evidence.position_gap = double(
            open.body->get_position().distance_to(gated.body->get_position())
        );
        evidence.linear_gap = double((open.body->get_linear_velocity()
                                      - gated.body->get_linear_velocity())
                                         .length());
        evidence.angular_gap = double((open.body->get_angular_velocity()
                                       - gated.body->get_angular_velocity())
                                          .length());
        release_world(open);
        release_world(gated);
        return false;
    }
};

struct HeldWriteEvidence {
    bool driven = false;
    Vector3 written;
    Vector3 held_pose;
    Vector3 first_solve_pose;
};

HeldWriteEvidence &held_write_evidence() {
    static HeldWriteEvidence evidence;
    return evidence;
}

class HeldWriteScenario final : public netw_test::FrameScenario {
    int step = 0;
    GateWorld world;

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        HeldWriteEvidence &evidence = held_write_evidence();
        if (step == 0) {
            world = seat_world();
            evidence.written = Vector3(3.0, real_t(BALL_RADIUS), -2.0);
            ++step;
            return true;
        }
        if (step == 1) {
            ++step;
            return true;
        }
        if (step == 2) {
            hold_world(world, true);
            world.body->set_position(evidence.written);
            world.body->set_linear_velocity(Vector3());
            world.body->set_angular_velocity(Vector3());
            ++step;
            return true;
        }
        if (step <= 7) {
            ++step;
            return true;
        }
        if (step == 8) {
            evidence.held_pose = world.body->get_position();
            hold_world(world, false);
            ++step;
            return true;
        }
        evidence.first_solve_pose = world.body->get_position();
        evidence.driven = true;
        release_world(world);
        return false;
    }
};

struct HoldStopsEvidence {
    bool driven = false;
    bool spaces_differ = false;
    double open_travel = 0.0;
    double held_travel = 0.0;
};

HoldStopsEvidence &hold_stops_evidence() {
    static HoldStopsEvidence evidence;
    return evidence;
}

class HoldStopsScenario final : public netw_test::FrameScenario {
    int step = 0;
    Vector3 open_start;
    Vector3 held_start;
    GateWorld open;
    GateWorld held;

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        HoldStopsEvidence &evidence = hold_stops_evidence();
        if (step == 0) {
            open = seat_world();
            held = seat_world();
            ++step;
            return true;
        }
        if (step == 1) {
            evidence.spaces_differ
                = stepping_space(open) != stepping_space(held);
            open.body->set_angular_velocity(Vector3(0.0, 0.0, 6.0));
            held.body->set_angular_velocity(Vector3(0.0, 0.0, 6.0));
            ++step;
            return true;
        }
        if (step == 2) {
            open_start = open.body->get_position();
            held_start = held.body->get_position();
            hold_world(held, true);
            ++step;
            return true;
        }
        if (step <= 32) {
            ++step;
            return true;
        }
        evidence.open_travel
            = double(open_start.distance_to(open.body->get_position()));
        evidence.held_travel
            = double(held_start.distance_to(held.body->get_position()));
        evidence.driven = true;
        hold_world(held, false);
        release_world(open);
        release_world(held);
        return false;
    }
};

struct SpaceIdentityEvidence {
    bool driven = false;
    bool declared_space_valid = false;
    bool declared_space_is_the_stepping_space = false;
    double declared_hold_travel = 0.0;
    double stepping_hold_travel = 0.0;
};

SpaceIdentityEvidence &space_identity_evidence() {
    static SpaceIdentityEvidence evidence;
    return evidence;
}

RID declared_space(const GateWorld &p_world) {
    return p_world.viewport->get_world_3d()->get_space();
}

class SpaceIdentityScenario final : public netw_test::FrameScenario {
    static constexpr int HOLD_FRAMES = 15;

    int step = 0;
    Vector3 start;
    GateWorld world;

    void spin() {
        world.body->set_angular_velocity(Vector3(0.0, 0.0, 6.0));
    }

    double travel() const {
        return double(start.distance_to(world.body->get_position()));
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        SpaceIdentityEvidence &evidence = space_identity_evidence();
        if (step == 0) {
            world = seat_world();
            ++step;
            return true;
        }
        if (step == 1) {
            evidence.declared_space_valid = declared_space(world).is_valid();
            evidence.declared_space_is_the_stepping_space
                = declared_space(world) == stepping_space(world);
            spin();
            if (evidence.declared_space_valid) {
                PhysicsServer3D::get_singleton()->space_set_active(
                    declared_space(world),
                    false
                );
            }
            ++step;
            return true;
        }
        if (step == 2) {
            start = world.body->get_position();
            ++step;
            return true;
        }
        if (step <= 2 + HOLD_FRAMES) {
            ++step;
            return true;
        }
        if (step == 3 + HOLD_FRAMES) {
            evidence.declared_hold_travel = travel();
            if (evidence.declared_space_valid) {
                PhysicsServer3D::get_singleton()->space_set_active(
                    declared_space(world),
                    true
                );
            }
            spin();
            hold_world(world, true);
            start = world.body->get_position();
            ++step;
            return true;
        }
        if (step <= 3 + HOLD_FRAMES * 2) {
            ++step;
            return true;
        }
        evidence.stepping_hold_travel = travel();
        evidence.driven = true;
        hold_world(world, false);
        release_world(world);
        return false;
    }
};

NETW_FRAME_SCENARIO(HoldSkipScenario, hold_one_frame_in_three, 3, 0);
NETW_FRAME_SCENARIO(HoldSkipScenario, hold_one_frame_in_seven, 7, 1);
NETW_FRAME_SCENARIO(HeldWriteScenario, held_write);
NETW_FRAME_SCENARIO(HoldStopsScenario, hold_stops);
NETW_FRAME_SCENARIO(SpaceIdentityScenario, space_identity);

void check_hold_is_a_pure_skip(int p_slot) {
    const HoldSkipEvidence &evidence = hold_skip_evidence(p_slot);
    REQUIRE(evidence.driven);
    NETW_CHECK_EQ(evidence.solves, GATE_SOLVES);
    NETW_CHECK_LT(evidence.position_gap, GATE_EPSILON);
    NETW_CHECK_LT(evidence.linear_gap, GATE_EPSILON);
    NETW_CHECK_LT(evidence.angular_gap, GATE_EPSILON);
}

} // namespace

TEST_CASE(
    "[Networked][LagComp][Frame] SG1 holding one frame in three is a pure "
    "skip"
) {
    check_hold_is_a_pure_skip(0);
}

TEST_CASE(
    "[Networked][LagComp][Frame] SG2 holding one frame in seven is a pure "
    "skip"
) {
    check_hold_is_a_pure_skip(1);
}

TEST_CASE(
    "[Networked][LagComp][Frame] SG3 a write during a held frame survives "
    "the hold and is what the next solve starts from"
) {
    const HeldWriteEvidence &evidence = held_write_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.held_pose.is_equal_approx(evidence.written));
    NETW_CHECK_LT(
        double(evidence.first_solve_pose.distance_to(evidence.written)),
        0.05
    );
}

TEST_CASE(
    "[Networked][LagComp][Frame] SG4 a hold stops a body that would have "
    "moved"
) {
    const HoldStopsEvidence &evidence = hold_stops_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.spaces_differ);
    NETW_CHECK_GT(evidence.open_travel, 0.01);
    NETW_CHECK_LT(evidence.held_travel, GATE_EPSILON);
}

TEST_CASE(
    "[Networked][LagComp][Frame] SG5 the space that steps a body is the one "
    "the body names, never the one its viewport declares"
) {
    const SpaceIdentityEvidence &evidence = space_identity_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.declared_space_valid);
    CHECK_FALSE(evidence.declared_space_is_the_stepping_space);
    NETW_CHECK_GT(evidence.declared_hold_travel, 0.01);
    NETW_CHECK_LT(evidence.stepping_hold_travel, GATE_EPSILON);
}

} // namespace NetwTests
