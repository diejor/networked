#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/collision_shape.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr double BODY_SPEED = 90.0;
constexpr double HALF_EXTENT = 8.0;
constexpr double FLOOR_Y = 200.0;
constexpr double WALL_FACE_X = 100.0;
constexpr double WALL_THICKNESS = 20.0;
constexpr double WALL_HEIGHT = 4000.0;

struct ReplayRun {
    bool walled;
    Vector2 start;
    Vector2 motion;
    int ticks;
};

const ReplayRun REPLAY_RUNS[] = {
    {false, Vector2(0.0, 0.0), Vector2(1.0, 0.0), 6},
    {true, Vector2(80.0, 0.0), Vector2(1.0, 1.0), 20},
    {false, Vector2(0.0, 150.0), Vector2(0.0, 1.0), 60},
    {false, Vector2(0.0, 0.0), Vector2(1.0, 0.0), 1},
};

constexpr int REPLAY_RUN_COUNT = int(sizeof(REPLAY_RUNS) / sizeof(ReplayRun));

struct RunEvidence {
    bool driven = false;
    Vector2 start;
    Vector2 reference;
    Vector2 replay;
    bool reference_on_floor = false;
    bool replay_on_floor = false;
};

struct ReplayEvidence {
    double physics_delta = 0.0;
    RunEvidence runs[REPLAY_RUN_COUNT];
};

ReplayEvidence &replay_evidence() {
    static ReplayEvidence evidence;
    return evidence;
}

Node2D *seat_arena(bool p_walled) {
    Node2D *arena = memnew(Node2D);

    StaticBody2D *floor_body = memnew(StaticBody2D);
    floor_body->set_position(Vector2(0.0, real_t(FLOOR_Y)));
    CollisionShape2D *floor_shape = memnew(CollisionShape2D);
    Ref<WorldBoundaryShape2D> boundary;
    boundary.instantiate();
    boundary->set_normal(Vector2(0.0, -1.0));
    boundary->set_distance(0.0);
    floor_shape->set_shape(boundary);
    floor_body->add_child(floor_shape);
    arena->add_child(floor_body);

    if (p_walled) {
        StaticBody2D *wall = memnew(StaticBody2D);
        wall->set_position(
            Vector2(real_t(WALL_FACE_X + WALL_THICKNESS * 0.5), 0.0)
        );
        CollisionShape2D *wall_shape = memnew(CollisionShape2D);
        Ref<RectangleShape2D> rectangle;
        rectangle.instantiate();
        rectangle->set_size(
            Vector2(real_t(WALL_THICKNESS), real_t(WALL_HEIGHT))
        );
        wall_shape->set_shape(rectangle);
        wall->add_child(wall_shape);
        arena->add_child(wall);
    }

    netw::gd::scene_root()->add_child(arena);
    return arena;
}

CharacterBody2D *seat_body(Node *p_arena, Vector2 p_start) {
    CharacterBody2D *body = memnew(CharacterBody2D);
    body->set_collision_layer(0);
    body->set_collision_mask(1);
    CollisionShape2D *shape = memnew(CollisionShape2D);
    Ref<RectangleShape2D> rectangle;
    rectangle.instantiate();
    rectangle->set_size(
        Vector2(real_t(HALF_EXTENT * 2.0), real_t(HALF_EXTENT * 2.0))
    );
    shape->set_shape(rectangle);
    body->add_child(shape);
    body->set_position(p_start);
    p_arena->add_child(body);
    return body;
}

void drive_tick(CharacterBody2D *p_body, Vector2 p_motion) {
    p_body->set_velocity(p_motion * real_t(BODY_SPEED));
    p_body->move_and_slide();
}

class ReplayScenario final : public netw_test::FrameScenario {
    int run_index = 0;
    int step = 0;
    Node2D *arena = nullptr;
    CharacterBody2D *reference = nullptr;
    CharacterBody2D *replay = nullptr;

    void open(const ReplayRun &p_run) {
        arena = seat_arena(p_run.walled);
        reference = seat_body(arena, p_run.start);
        replay = seat_body(arena, p_run.start);
    }

    void close(const ReplayRun &p_run) {
        RunEvidence &evidence = replay_evidence().runs[run_index];
        evidence.driven = true;
        evidence.start = p_run.start;
        evidence.reference = reference->get_position();
        evidence.replay = replay->get_position();
        evidence.reference_on_floor = reference->is_on_floor();
        evidence.replay_on_floor = replay->is_on_floor();

        netw::gd::scene_root()->remove_child(arena);
        memdelete(arena);
        arena = nullptr;
        reference = nullptr;
        replay = nullptr;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr
            || run_index >= REPLAY_RUN_COUNT) {
            return false;
        }
        const ReplayRun &run = REPLAY_RUNS[run_index];
        if (step == 0) {
            replay_evidence().physics_delta
                = 1.0
                / double(
                      Engine::get_singleton()->get_physics_ticks_per_second()
                );
            open(run);
            ++step;
            return true;
        }
        if (step <= run.ticks) {
            drive_tick(reference, run.motion);
            ++step;
            return true;
        }
        if (step == run.ticks + 1) {
            for (int tick = 0; tick < run.ticks; ++tick) {
                drive_tick(replay, run.motion);
            }
            ++step;
            return true;
        }
        close(run);
        ++run_index;
        step = 0;
        return run_index < REPLAY_RUN_COUNT;
    }
};

NETW_FRAME_SCENARIO(ReplayScenario, replay_scenario);

const RunEvidence &run_evidence(int p_index) {
    return replay_evidence().runs[p_index];
}

} // namespace

TEST_CASE(
    "[Networked][LagComp][Frame] KR1 a batched replay in one frame lands "
    "where the same ticks land one per frame"
) {
    const RunEvidence &evidence = run_evidence(0);
    REQUIRE(evidence.driven);
    CHECK(evidence.reference.is_equal_approx(evidence.replay));
    NETW_CHECK_GT(evidence.reference.x, evidence.start.x + 1.0);
}

TEST_CASE(
    "[Networked][LagComp][Frame] KR2 a batched replay reproduces the wall "
    "slide the per-frame drive produced"
) {
    const RunEvidence &evidence = run_evidence(1);
    REQUIRE(evidence.driven);
    CHECK(evidence.reference.is_equal_approx(evidence.replay));
    NETW_CHECK_LT(evidence.replay.x, 95.0);
    NETW_CHECK_GT(evidence.replay.y, evidence.start.y + 5.0);
}

TEST_CASE(
    "[Networked][LagComp][Frame] KR3 the floor flag after a batched replay "
    "is the flag the per-frame drive ends on"
) {
    const RunEvidence &evidence = run_evidence(2);
    REQUIRE(evidence.driven);
    CHECK(evidence.reference_on_floor);
    CHECK(evidence.replay_on_floor);
    CHECK(evidence.reference.is_equal_approx(evidence.replay));
}

TEST_CASE(
    "[Networked][LagComp][Frame] KR4 one replayed tick advances exactly one "
    "physics tick of motion"
) {
    const RunEvidence &evidence = run_evidence(3);
    REQUIRE(evidence.driven);
    const double travel = double(evidence.replay.x - evidence.start.x);
    NETW_CHECK_CLOSE(
        travel,
        BODY_SPEED * replay_evidence().physics_delta,
        0.05
    );
}

} // namespace NetwTests
