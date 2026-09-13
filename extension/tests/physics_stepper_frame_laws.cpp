#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/collision_shape.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/raycast.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/world.hpp"
#include "netw/api/predict.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr double STEP_DELTA = 1.0 / 60.0;
constexpr double FLOOR_THICKNESS = 2.0;
constexpr double FLOOR_SPAN = 400.0;
constexpr double BODY_RADIUS = 0.5;
constexpr double DROP_HEIGHT = 6.0;
constexpr double RESTORE_HEIGHT = BODY_RADIUS + 0.05;
constexpr double SENSOR_REACH = 1.0;
constexpr double CONTACT_SPEED = 6.0;
constexpr double CONTACT_OFFSET = 0.35;
constexpr double CONTACT_GAP = 3.0;
constexpr int FALL_STEPS = 10;
constexpr int REST_STEPS = 40;
constexpr int CONTACT_STEPS = 24;
constexpr double AGREEMENT = 1e-3;

constexpr double CAR_GAP = 1.5;
constexpr double CAR_SPEED = 8.0;
constexpr double CAR_LENGTH = 2.0;
constexpr double CAR_WIDTH = 1.0;
constexpr double CAR_HEIGHT = 0.5;
constexpr double CAR_SEAT = CAR_HEIGHT * 0.5 + 0.05;
constexpr int APPROACH_CAP = 120;
constexpr int REWIND_TICKS = 2;
constexpr double REWIND_POSE_TOLERANCE = 0.0;
constexpr double FLOAT32_QUATERNION_TURN_FLOOR = 1.0e-3;

const Vector3 RELAY_IMPULSE(0.0, 1.0, 0.5);

struct PoseSample {
    Vector3 position;
    Quaternion rotation;
    Vector3 linear;
    Vector3 angular;
    bool sleeping = false;
    bool taken = false;
};

enum Drive { PER_FRAME, BATCHED, STALLED };

struct Run {
    bool floored;
    bool contact;
    Drive drive;
    int steps;
};

const Run RUNS[] = {
    {false, false, PER_FRAME, FALL_STEPS},
    {false, false, BATCHED, FALL_STEPS},
    {true, false, PER_FRAME, REST_STEPS},
    {true, false, BATCHED, REST_STEPS},
    {true, true, PER_FRAME, CONTACT_STEPS},
    {true, true, PER_FRAME, CONTACT_STEPS},
    {false, false, STALLED, FALL_STEPS},
};

constexpr int RUN_COUNT = int(sizeof(RUNS) / sizeof(Run));

struct Rewind {
    bool car;
    double gap;
    double speed;
    double seat;
};

const Rewind REWINDS[] = {
    {false, CONTACT_GAP, CONTACT_SPEED, RESTORE_HEIGHT},
    {true, CAR_GAP, CAR_SPEED, CAR_SEAT},
};

constexpr int REWIND_COUNT = int(sizeof(REWINDS) / sizeof(Rewind));

struct RewindEvidence {
    bool taken = false;
    bool contact = false;
    int contact_tick = -1;
    int verdict = netw::NetwPredict::EXACT_VERDICT_UNJUDGED;
    double residual = 0.0;
    double turn = 0.0;
    PoseSample forward[2];
    PoseSample replay[2];
};

struct StepperEvidence {
    bool fork = false;
    bool driven = false;

    double held_drift = 0.0;
    double stepped_drop = 0.0;

    bool hold_took = false;
    bool hold_survived = false;

    PoseSample runs[RUN_COUNT][2];
    PoseSample runs_at_hold[RUN_COUNT];

    bool barrier_sensor_reads_floor = false;
    bool bare_sensor_reads_floor = false;

    RewindEvidence rewinds[REWIND_COUNT];
};

StepperEvidence &evidence() {
    static StepperEvidence held;
    return held;
}

PhysicsServer3D *server() {
    return PhysicsServer3D::get_singleton();
}

bool engine_steps_spaces() {
    return server() != nullptr
        && server()->has_method(StringName("space_step"));
}

RID arena_space() {
    Window *root = Object::cast_to<Window>(netw::gd::scene_root());
    if (root == nullptr) {
        return RID();
    }
    const Ref<World3D> world = root->find_world_3d();
    return world.is_valid() ? world->get_space() : RID();
}

void step_space() {
    const RID space = arena_space();
    Array one;
    one.push_back(space);
    if (server()->has_method(StringName("space_flush_queries"))) {
        server()->callv(StringName("space_flush_queries"), one);
    }
    Array args;
    args.push_back(space);
    args.push_back(STEP_DELTA);
    server()->callv(StringName("space_step"), args);
}

Node3D *seat_arena(bool p_floored) {
    Node3D *arena = memnew(Node3D);
    if (p_floored) {
        StaticBody3D *floor_body = memnew(StaticBody3D);
        floor_body->set_position(
            Vector3(0.0, real_t(-FLOOR_THICKNESS * 0.5), 0.0)
        );
        CollisionShape3D *shape = memnew(CollisionShape3D);
        Ref<BoxShape3D> box;
        box.instantiate();
        box->set_size(Vector3(
            real_t(FLOOR_SPAN),
            real_t(FLOOR_THICKNESS),
            real_t(FLOOR_SPAN)
        ));
        shape->set_shape(box);
        floor_body->add_child(shape);
        arena->add_child(floor_body);
    }
    netw::gd::scene_root()->add_child(arena);
    return arena;
}

RigidBody3D *seat_body(Node *p_arena, Vector3 p_at, Vector3 p_linear) {
    RigidBody3D *body = memnew(RigidBody3D);
    body->set_can_sleep(false);
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<SphereShape3D> sphere;
    sphere.instantiate();
    sphere->set_radius(real_t(BODY_RADIUS));
    shape->set_shape(sphere);
    body->add_child(shape);
    body->set_position(p_at);
    body->set_linear_velocity(p_linear);
    p_arena->add_child(body);
    return body;
}

RigidBody3D *seat_car(Node *p_arena, Vector3 p_at, Vector3 p_linear) {
    RigidBody3D *body = memnew(RigidBody3D);
    body->set_can_sleep(false);
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(
        Vector3(real_t(CAR_LENGTH), real_t(CAR_HEIGHT), real_t(CAR_WIDTH))
    );
    shape->set_shape(box);
    body->add_child(shape);
    body->set_position(p_at);
    body->set_linear_velocity(p_linear);
    p_arena->add_child(body);
    return body;
}

PoseSample sample_of(RigidBody3D *p_body) {
    PoseSample sample;
    PhysicsDirectBodyState3D *state
        = server()->body_get_direct_state(p_body->get_rid());
    if (state == nullptr) {
        return sample;
    }
    const Transform3D pose = state->get_transform();
    sample.position = pose.origin;
    sample.rotation = pose.basis.get_rotation_quaternion();
    sample.linear = state->get_linear_velocity();
    sample.angular = state->get_angular_velocity();
    sample.sleeping = state->is_sleeping();
    sample.taken = true;
    return sample;
}

void restore_body(RigidBody3D *p_body, const PoseSample &p_sample) {
    PhysicsDirectBodyState3D *state
        = server()->body_get_direct_state(p_body->get_rid());
    if (state == nullptr) {
        return;
    }
    Transform3D pose = state->get_transform();
    pose.origin = p_sample.position;
    pose.basis = Basis(p_sample.rotation);
    state->set_transform(pose);
    state->set_linear_velocity(p_sample.linear);
    state->set_angular_velocity(p_sample.angular);
    state->set_sleep_state(p_sample.sleeping);
}

double residual_between(const PoseSample &p_once, const PoseSample &p_twice) {
    double most = double(p_once.position.distance_to(p_twice.position));
    const double linear = double(p_once.linear.distance_to(p_twice.linear));
    const double angular = double(p_once.angular.distance_to(p_twice.angular));
    if (linear > most) {
        most = linear;
    }
    if (angular > most) {
        most = angular;
    }
    return most;
}

double turn_between(const PoseSample &p_once, const PoseSample &p_twice) {
    return double(p_once.rotation.angle_to(p_twice.rotation));
}

void retire(Node3D *&p_arena) {
    netw::gd::scene_root()->remove_child(p_arena);
    memdelete(p_arena);
    p_arena = nullptr;
}

class StepperScenario final : public netw_test::FrameScenario {
    int stage = 0;
    int run_index = 0;
    int rewind_index = 0;
    int step = 0;
    Node3D *arena = nullptr;
    RigidBody3D *first = nullptr;
    RigidBody3D *second = nullptr;
    RayCast3D *sensor = nullptr;
    int contact_tick = -1;
    PoseSample basis[2];

    void hold() {
        server()->space_set_active(arena_space(), false);
    }

    void release() {
        server()->space_set_active(arena_space(), true);
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (stage == 0) {
            evidence().fork = engine_steps_spaces();
            if (!evidence().fork) {
                return false;
            }
            ++stage;
            return true;
        }
        if (stage == 1) {
            return hold_stage();
        }
        if (stage == 2) {
            return run_stage();
        }
        if (stage == 3) {
            return barrier_stage();
        }
        if (stage == 4) {
            return rewind_stage();
        }
        evidence().driven = true;
        return false;
    }

private:
    bool hold_stage() {
        if (step == 0) {
            arena = seat_arena(false);
            first = seat_body(
                arena,
                Vector3(0.0, real_t(DROP_HEIGHT), 0.0),
                Vector3()
            );
            ++step;
            return true;
        }
        if (step == 1) {
            hold();
            evidence().hold_took = !server()->space_is_active(arena_space())
                && arena_space() == first->get_world_3d()->get_space();
            ++step;
            return true;
        }
        if (step == 2) {
            evidence().held_drift = double(sample_of(first).position.y);
            ++step;
            return true;
        }
        if (step == 3) {
            evidence().hold_survived
                = !server()->space_is_active(arena_space());
            const double before = double(sample_of(first).position.y);
            evidence().held_drift = evidence().held_drift - before;
            evidence().stepped_drop = before;
            step_space();
            ++step;
            return true;
        }
        if (step == 4) {
            evidence().stepped_drop -= double(sample_of(first).position.y);
            ++step;
            return true;
        }
        release();
        retire(arena);
        first = nullptr;
        ++stage;
        step = 0;
        return true;
    }

    bool run_stage() {
        const Run &run = RUNS[run_index];
        if (step == 0) {
            open(run);
            ++step;
            return true;
        }
        if (step == 1) {
            hold();
            evidence().runs_at_hold[run_index] = sample_of(first);
            ++step;
            return true;
        }
        if (run.drive == BATCHED) {
            if (step == 2) {
                for (int tick = 0; tick < run.steps; ++tick) {
                    step_space();
                }
                ++step;
                return true;
            }
        } else if (step <= run.steps + 1) {
            if (run.drive == PER_FRAME) {
                step_space();
            }
            ++step;
            return true;
        }
        close();
        ++run_index;
        step = 0;
        if (run_index < RUN_COUNT) {
            return true;
        }
        ++stage;
        return true;
    }

    void open(const Run &p_run) {
        arena = seat_arena(p_run.floored);
        if (p_run.contact) {
            first = seat_body(
                arena,
                Vector3(
                    real_t(-CONTACT_GAP),
                    real_t(RESTORE_HEIGHT),
                    real_t(CONTACT_OFFSET)
                ),
                Vector3(real_t(CONTACT_SPEED), 0.0, 0.0)
            );
            second = seat_body(
                arena,
                Vector3(
                    real_t(CONTACT_GAP),
                    real_t(RESTORE_HEIGHT),
                    real_t(-CONTACT_OFFSET)
                ),
                Vector3(real_t(-CONTACT_SPEED), 0.0, 0.0)
            );
            return;
        }
        first = seat_body(
            arena,
            Vector3(0.0, real_t(DROP_HEIGHT), 0.0),
            Vector3()
        );
    }

    void close() {
        PoseSample *into = evidence().runs[run_index];
        into[0] = sample_of(first);
        if (second != nullptr) {
            into[1] = sample_of(second);
        }
        release();
        retire(arena);
        first = nullptr;
        second = nullptr;
    }

    bool barrier_stage() {
        if (step == 0) {
            arena = seat_arena(true);
            first = seat_body(
                arena,
                Vector3(0.0, real_t(DROP_HEIGHT), 0.0),
                Vector3()
            );
            sensor = memnew(RayCast3D);
            sensor->set_enabled(false);
            sensor->set_target_position(
                Vector3(0.0, real_t(-SENSOR_REACH), 0.0)
            );
            first->add_child(sensor);
            sensor->add_exception(first);
            ++step;
            return true;
        }
        if (step == 1) {
            hold();
            ++step;
            return true;
        }
        if (step == 2) {
            first->set_global_position(
                Vector3(0.0, real_t(RESTORE_HEIGHT), 0.0)
            );
            sensor->force_raycast_update();
            evidence().bare_sensor_reads_floor = sensor->is_colliding();
            ++step;
            return true;
        }
        if (step == 3) {
            first->set_global_position(
                Vector3(real_t(CONTACT_GAP), real_t(RESTORE_HEIGHT), 0.0)
            );
            first->force_update_transform();
            sensor->force_raycast_update();
            evidence().barrier_sensor_reads_floor = sensor->is_colliding();
            ++step;
            return true;
        }
        release();
        retire(arena);
        first = nullptr;
        sensor = nullptr;
        ++stage;
        step = 0;
        return true;
    }

    bool rewind_stage() {
        const Rewind &rewind = REWINDS[rewind_index];
        if (step == 0 || step == 2) {
            open_rewind(rewind);
            ++step;
            return true;
        }
        if (step == 1) {
            hold();
            contact_tick = probe_contact();
            close_rewind();
            ++step;
            return true;
        }
        if (step == 3) {
            hold();
            measure_rewind();
            close_rewind();
            ++step;
            return true;
        }
        ++rewind_index;
        step = 0;
        if (rewind_index < REWIND_COUNT) {
            return true;
        }
        ++stage;
        return true;
    }

    void open_rewind(const Rewind &p_rewind) {
        arena = seat_arena(true);
        const Vector3 near_side(
            real_t(-p_rewind.gap),
            real_t(p_rewind.seat),
            real_t(CONTACT_OFFSET)
        );
        const Vector3 closing(real_t(p_rewind.speed), 0.0, 0.0);
        first = p_rewind.car ? seat_car(arena, near_side, closing)
                             : seat_body(arena, near_side, closing);
        const Vector3 far_side(
            real_t(p_rewind.gap),
            real_t(RESTORE_HEIGHT),
            real_t(-CONTACT_OFFSET)
        );
        second = seat_body(
            arena,
            far_side,
            p_rewind.car ? Vector3()
                         : Vector3(real_t(-p_rewind.speed), 0.0, 0.0)
        );
        first->set_contact_monitor(true);
        first->set_max_contacts_reported(8);
    }

    void close_rewind() {
        release();
        retire(arena);
        first = nullptr;
        second = nullptr;
    }

    bool touching() const {
        PhysicsDirectBodyState3D *state
            = server()->body_get_direct_state(first->get_rid());
        if (state == nullptr) {
            return false;
        }
        for (int index = 0; index < state->get_contact_count(); ++index) {
            if (state->get_contact_collider(index) == second->get_rid()) {
                return true;
            }
        }
        return false;
    }

    int probe_contact() {
        for (int tick = 1; tick <= APPROACH_CAP; ++tick) {
            step_space();
            if (touching()) {
                return tick;
            }
        }
        return -1;
    }

    bool drive_rewind() {
        bool touched = false;
        for (int tick = 0; tick < REWIND_TICKS; ++tick) {
            if (tick == 0) {
                server()->body_apply_central_impulse(
                    first->get_rid(),
                    RELAY_IMPULSE
                );
            }
            step_space();
            touched = touched || touching();
        }
        return touched;
    }

    void measure_rewind() {
        RewindEvidence &into = evidence().rewinds[rewind_index];
        into.contact_tick = contact_tick;
        const int lead
            = contact_tick > REWIND_TICKS ? contact_tick - REWIND_TICKS : 0;
        for (int tick = 0; tick < lead; ++tick) {
            step_space();
        }
        basis[0] = sample_of(first);
        basis[1] = sample_of(second);

        into.contact = drive_rewind();
        into.forward[0] = sample_of(first);
        into.forward[1] = sample_of(second);

        restore_body(first, basis[0]);
        restore_body(second, basis[1]);
        drive_rewind();
        into.replay[0] = sample_of(first);
        into.replay[1] = sample_of(second);

        for (int body = 0; body < 2; ++body) {
            const double moved
                = residual_between(into.forward[body], into.replay[body]);
            const double turned
                = turn_between(into.forward[body], into.replay[body]);
            if (moved > into.residual) {
                into.residual = moved;
            }
            if (turned > into.turn) {
                into.turn = turned;
            }
        }
        const bool sleeps_alike
            = into.forward[0].sleeping == into.replay[0].sleeping
            && into.forward[1].sleeping == into.replay[1].sleeping;
        into.verdict = into.residual <= REWIND_POSE_TOLERANCE
                && into.turn <= FLOAT32_QUATERNION_TURN_FLOOR && sleeps_alike
            ? netw::NetwPredict::EXACT_VERDICT_EQUAL
            : netw::NetwPredict::EXACT_VERDICT_UNEQUAL;
        into.taken = true;
    }
};

NETW_FRAME_SCENARIO(StepperScenario, stepper_scenario);

const char *SKIP_REASON
    = "PhysicsServer3D has no space_step, so this engine is not the "
      "stepping-physics fork and the stepper drive cannot be exercised.";

bool skipped() {
    REQUIRE_MESSAGE(true, SKIP_REASON);
    return !evidence().fork;
}

double fall_while_held(int p_run) {
    const PoseSample &at_hold = evidence().runs_at_hold[p_run];
    const PoseSample &at_close = evidence().runs[p_run][0];
    REQUIRE(at_hold.taken);
    REQUIRE(at_close.taken);
    return double(at_hold.position.y) - double(at_close.position.y);
}

void agrees(const PoseSample &p_once, const PoseSample &p_twice) {
    REQUIRE(p_once.taken);
    REQUIRE(p_twice.taken);
    NETW_CHECK_LT(
        double(p_once.position.distance_to(p_twice.position)),
        AGREEMENT
    );
    NETW_CHECK_LT(double(p_once.linear.distance_to(p_twice.linear)), AGREEMENT);
    NETW_CHECK_LT(
        double(p_once.angular.distance_to(p_twice.angular)),
        AGREEMENT
    );
    NETW_CHECK_LT(
        double(p_once.rotation.angle_to(p_twice.rotation)),
        AGREEMENT
    );
    NETW_CHECK_EQ(p_once.sleeping, p_twice.sleeping);
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Frame] X1 a held space integrates nothing across a "
    "physics frame and exactly one tick of gravity when it is stepped"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    CHECK(evidence().hold_took);
    CHECK(evidence().hold_survived);
    NETW_CHECK_LT(evidence().held_drift, 1e-6);
    NETW_CHECK_GT(evidence().stepped_drop, 0.0);
    NETW_CHECK_LT(evidence().stepped_drop, 0.05);
}

TEST_CASE(
    "[Networked][Predict][Frame] X2 a batched replay of N steps lands where N "
    "steps taken one per frame landed, falling and resting alike"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    NETW_CHECK_LT(fall_while_held(6), AGREEMENT);
    agrees(evidence().runs[0][0], evidence().runs[1][0]);
    agrees(evidence().runs[2][0], evidence().runs[3][0]);
    NETW_CHECK_GT(double(evidence().runs[2][0].position.y), 0.0);
}

TEST_CASE(
    "[Networked][Predict][Frame] X0 the same off-centre contact driven twice "
    "reproduces itself, which is the reading X3's verdict cannot be "
    "interpreted without"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    NETW_CHECK_LT(
        double(evidence().runs[4][0].position.x),
        real_t(-CONTACT_GAP) + CONTACT_SPEED * CONTACT_STEPS * STEP_DELTA
    );
    agrees(evidence().runs[4][0], evidence().runs[5][0]);
    agrees(evidence().runs[4][1], evidence().runs[5][1]);
}

TEST_CASE(
    "[Networked][Predict][Frame] X3 a relayed impulse replayed from a basis "
    "two ticks back over a live contact records its verdict, and the residual "
    "it measures is the tolerance the example claims"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    const RewindEvidence &rewind = evidence().rewinds[0];
    REQUIRE(rewind.taken);
    CHECK(rewind.contact);
    NETW_CHECK_GT(rewind.contact_tick, REWIND_TICKS);
    CHECK(rewind.verdict != netw::NetwPredict::EXACT_VERDICT_UNJUDGED);
    NETW_CHECK_LE(rewind.residual, REWIND_POSE_TOLERANCE);
    NETW_CHECK_LE(rewind.turn, FLOAT32_QUATERNION_TURN_FLOOR);
}

TEST_CASE(
    "[Networked][Predict][Frame] X5 a stepped car-ball contact replayed from "
    "a basis reproduces the forward contact's post-state within the tolerance "
    "X3 measured"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    const RewindEvidence &rewind = evidence().rewinds[1];
    REQUIRE(rewind.taken);
    CHECK(rewind.contact);
    NETW_CHECK_GT(double(rewind.forward[1].linear.length()), 0.5);
    NETW_CHECK_LE(rewind.residual, REWIND_POSE_TOLERANCE);
    NETW_CHECK_LE(rewind.turn, FLOAT32_QUATERNION_TURN_FLOOR);
    NETW_CHECK_EQ(rewind.verdict, netw::NetwPredict::EXACT_VERDICT_EQUAL);
}

TEST_CASE(
    "[Networked][Predict][Frame] X4 a sensor sampled after a restore forced "
    "through to the server reads the world the restore produced"
) {
    if (skipped()) {
        return;
    }
    REQUIRE(evidence().driven);
    CHECK(evidence().barrier_sensor_reads_floor);
}

} // namespace NetwTests
