#include "support/netw_test.h"

#include "support/fault_log.h"
#include "support/installable_stepper.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"
#include "netw/predict/engine.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwPredictSteppedLaws {

using namespace netw_test;
using godot::Ref;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPrediction;
using netw::NetwPredictionEngine;

EntityDecl island_player(const char *p_name, netw::Schedule p_schedule) {
    return EntityDecl()
        .named(p_name)
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(godot::Vector2())
        .predicted(0.01)
        .scheduled(p_schedule);
}

Scenario rerunnable_island_lane() {
    Scenario scenario;
    scenario.label = "rerunnable-island";
    scenario.epsilon = 0.01;
    godot::Vector<godot::StringName> members;
    members.push_back(godot::StringName("Q"));
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(island_player("P", netw::Schedule::TICK), 0)
        .player(island_player("Q", netw::Schedule::TICK), 1)
        .island(godot::StringName("P"), members)
        .simulating(godot::StringName("Q"));
    scenario.clients = 2;
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", godot::Vector2(1.0, 0.0));
    return scenario.until(60);
}

Ref<netw::NetwPhysicsStepper> installable_stepper() {
    Ref<netw_test::InstallableStepper> made;
    made.instantiate();
    return made;
}

struct JointOwner {
    Ref<NetwEntity> entity;
    int64_t slot = -1;
};

JointOwner joint_owner(NetwMultiplayer *p_core) {
    JointOwner out;
    if (p_core == nullptr) {
        return out;
    }
    NetwPredictionEngine *const pool = p_core->get_prediction_engine();
    if (pool == nullptr) {
        return out;
    }
    const godot::TypedArray<NetwEntity> held
        = p_core->predict_stepped_entities();
    for (int at = 0; at < held.size(); ++at) {
        const Ref<NetwEntity> entity = held[at];
        if (entity.is_null()) {
            continue;
        }
        const int64_t seat = pool->slot_of(entity);
        if (seat >= 0
            && pool->island_of(seat) == NetwPredictionEngine::ISLAND_JOINT) {
            out.entity = entity;
            out.slot = seat;
            break;
        }
    }
    return out;
}

int64_t joint_passes(NetwPredictionEngine *p_pool, int64_t p_slot) {
    return p_pool->joint_stats(p_slot)[NetwPredictionEngine::STAT_JOINT_PASSES];
}

void step_onto(NetwMultiplayer *p_core, const JointOwner &p_owner) {
    godot::Node *body = p_owner.entity->get_owner();
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID own = p_core->entity_space_of(p_owner.entity).space;
    REQUIRE(own.is_valid());
    p_core->predict_stepper_install(own, installable_stepper());
    p_owner.entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
}

void unstep(const JointOwner &p_owner) {
    netw::gd::scene_root()->remove_child(p_owner.entity->get_owner());
}

TEST_CASE(
    "[Networked][Predict][Stepped] the tick drive re-admits an island owner "
    "on every rerunnable schedule, so a member cleared to INDEPENDENT is "
    "seated JOINT again on STEPPED exactly as it is on TICK"
) {
    const Scenario scenario = rerunnable_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const server = rig.server();
    REQUIRE(server != nullptr);
    NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const JointOwner owner = joint_owner(server);
    REQUIRE_MESSAGE(owner.slot >= 0, "no member seated JOINT after the run");

    const Ref<NetwPrediction> handle = owner.entity->get_prediction();
    REQUIRE(handle.is_valid());
    NETW_CHECK_EQ(
        handle->get_reconcile_mode(),
        int(netw::NetwPrediction::RECONCILE_JOINT)
    );
    NETW_CHECK_EQ(
        pool->reconcile_of(owner.slot),
        int(netw::NetwPrediction::RECONCILE_JOINT)
    );

    const auto clear = [&]() {
        pool->set_reconcile_own(
            owner.slot,
            int(netw::NetwPrediction::RECONCILE_INDEPENDENT)
        );
        pool->leave_joint(owner.slot);
        REQUIRE(
            pool->reconcile_of(owner.slot)
            == int(netw::NetwPrediction::RECONCILE_INDEPENDENT)
        );
    };

    SUBCASE("TICK re-admits within a tick") {
        clear();
        rig.step_ticks(4);
        NETW_CHECK_EQ(
            pool->reconcile_of(owner.slot),
            int(netw::NetwPrediction::RECONCILE_JOINT)
        );
        NETW_CHECK_EQ(
            pool->island_of(owner.slot),
            int(NetwPredictionEngine::ISLAND_JOINT)
        );
    }

    SUBCASE("STEPPED re-admits once its space holds a stepper") {
        step_onto(server, owner);
        REQUIRE(pool->slot_is_steppable(owner.slot));
        NETW_CHECK_EQ(
            pool->admitted_reconcile_mode(owner.slot),
            int(netw::NetwPrediction::RECONCILE_JOINT)
        );

        clear();
        rig.step_ticks(4);
        NETW_CHECK_EQ(
            pool->reconcile_of(owner.slot),
            int(netw::NetwPrediction::RECONCILE_JOINT)
        );
        NETW_CHECK_EQ(
            pool->island_of(owner.slot),
            int(NetwPredictionEngine::ISLAND_JOINT)
        );
        unstep(owner);
    }
}

TEST_CASE(
    "[Networked][Predict][Stepped] a predictor whose island owner turns "
    "STEPPED keeps closing joint passes over the same roster, so admission "
    "carries through to the replay and not only to the seat"
) {
    const Scenario scenario = rerunnable_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const JointOwner owner = joint_owner(predictor);
    REQUIRE_MESSAGE(owner.slot >= 0, "the predictor seated no JOINT owner");

    const int64_t members = pool->joint_stats(
        owner.slot
    )[NetwPredictionEngine::STAT_JOINT_MEMBERS];
    REQUIRE(members >= 2);
    const int64_t on_tick = joint_passes(pool, owner.slot);
    REQUIRE(on_tick > 0);

    step_onto(predictor, owner);
    rig.step_ticks(8);

    CHECK(joint_passes(pool, owner.slot) > on_tick);
    NETW_CHECK_EQ(
        pool->joint_stats(owner.slot)[NetwPredictionEngine::STAT_JOINT_MEMBERS],
        members
    );
    NETW_CHECK_EQ(
        pool->island_of(owner.slot),
        int(NetwPredictionEngine::ISLAND_JOINT)
    );
    unstep(owner);
}

TEST_CASE(
    "[Networked][Predict][Stepped] STEPPED is refused loudly, so a member "
    "whose space nothing can step resolves to SCHEDULE_FRAME, says "
    "so once, and keeps the declaration the game made"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][prediction] NetwPrediction.RECONCILE_JOINT needs "
        "every entity in the group of '*' to replay a tick, and '*'"
        " on SCHEDULE_STEPPED cannot, so the group runs "
        "NetwPrediction.RECONCILE_INDEPENDENT. Give each one a "
        "NetwSimulation.step on SCHEDULE_TICK, or use "
        "SCHEDULE_STEPPED.",
        1
    );

    const Scenario scenario = rerunnable_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const JointOwner owner = joint_owner(predictor);
    REQUIRE_MESSAGE(owner.slot >= 0, "the predictor seated no JOINT owner");
    NETW_CHECK_EQ(
        pool->schedule_of(owner.slot),
        int(netw::NetwSimulation::SCHEDULE_TICK)
    );

    const FaultWatch watch;
    owner.entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
    rig.step_ticks(4);

    CHECK_FALSE(pool->slot_has_stepper(owner.slot));
    NETW_CHECK_EQ(
        pool->schedule_of(owner.slot),
        int(netw::NetwSimulation::SCHEDULE_FRAME)
    );
    NETW_CHECK_EQ(
        owner.entity->get_simulation()->get_schedule(),
        netw::NetwSimulation::SCHEDULE_STEPPED
    );

    const int64_t driven_before = pool->last_driven_input_tick_of(owner.slot);
    rig.step_frame(4);
    NETW_CHECK_GE(
        pool->last_driven_input_tick_of(owner.slot) - driven_before,
        3
    );
    NETW_CHECK_EQ(
        watch.warnings_containing("cannot step physics on demand"),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][Predict][Stepped] a stepper installed after a member turns "
    "STEPPED but before that member is driven is not refused, so a game may "
    "install it once its scene is ready"
) {
    const Scenario scenario = rerunnable_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const JointOwner owner = joint_owner(predictor);
    REQUIRE_MESSAGE(owner.slot >= 0, "the predictor seated no JOINT owner");

    const FaultWatch watch;
    netw::gd::scene_root()->add_child(owner.entity->get_owner());
    owner.entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
    REQUIRE(
        pool->slot_has_stepper(owner.slot)
        == netw::gd::can_step_space(
            predictor->entity_space_of(owner.entity).dimension
        )
    );
    predictor->predict_stepper_install(
        predictor->entity_space_of(owner.entity).space,
        installable_stepper()
    );
    const int64_t driven_before = pool->last_driven_input_tick_of(owner.slot);
    for (int frame = 0; frame < 4; ++frame) {
        rig.step_frame(1);
    }

    NETW_CHECK_GE(
        pool->last_driven_input_tick_of(owner.slot) - driven_before,
        3
    );
    NETW_CHECK_EQ(
        pool->schedule_of(owner.slot),
        int(netw::NetwSimulation::SCHEDULE_STEPPED)
    );
    NETW_CHECK_EQ(
        watch.warnings_containing("cannot step physics on demand"),
        int64_t(0)
    );
    unstep(owner);
}

TEST_CASE(
    "[Networked][Predict][Stepped] JR1 a JOINT group set up in a legal order "
    "reports no refusal, and a member that later stops replaying is still "
    "reported once"
) {
    const Scenario scenario = rerunnable_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const JointOwner owner = joint_owner(predictor);
    REQUIRE_MESSAGE(owner.slot >= 0, "the predictor seated no JOINT owner");

    const char *refused = "RECONCILE_JOINT needs every entity";
    const FaultWatch watch;
    netw::gd::scene_root()->add_child(owner.entity->get_owner());
    owner.entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
    predictor->predict_stepper_install(
        predictor->entity_space_of(owner.entity).space,
        installable_stepper()
    );
    for (int frame = 0; frame < 4; ++frame) {
        rig.step_frame(1);
    }
    NETW_CHECK_EQ(watch.errors_containing(refused), int64_t(0));

    owner.entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_FRAME
    );
    for (int frame = 0; frame < 4; ++frame) {
        rig.step_frame(1);
    }
    NETW_CHECK_EQ(watch.errors_containing(refused), int64_t(1));
    unstep(owner);
}

Scenario consumed_lane() {
    Scenario scenario;
    scenario.label = "stepped-consumer";
    scenario.epsilon = 0.01;
    scenario.world.clocked(60, 3).lag_compensated().player(
        EntityDecl()
            .named("P")
            .on_schema("PredictedPose")
            .synced("position")
            .placed_at(godot::Vector2())
            .predicted(0.01)
            .scheduled(netw::Schedule::FRAME),
        0
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(30);
}

int64_t fact_of(const Ref<NetwPrediction> &p_handle, const char *p_name) {
    return int64_t(p_handle->get_stats()->to_dictionary().get(
        godot::StringName(p_name),
        int64_t(-1)
    ));
}

constexpr int STEPPED_FRAMES = 40;

godot::Node *stepped_body(
    NetwMultiplayer *p_core,
    const Ref<NetwPrediction> &p_handle,
    bool p_declared
) {
    const Ref<NetwEntity> entity = p_handle->get_entity();
    godot::Node *body = entity->get_owner();
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID space = p_core->entity_space_of(entity).space;
    REQUIRE(space.is_valid());
    p_core->predict_stepper_install(space, installable_stepper());
    if (p_declared) {
        entity->get_simulation()->set_schedule(
            netw::NetwSimulation::SCHEDULE_STEPPED
        );
    }
    return body;
}

struct SteppedRun {
    int64_t consumed = 0;
    int64_t ack = -1;
    int64_t closed = -1;
    int64_t confirmed_before = -1;
    int64_t confirmed = -1;
    int64_t corrections = 0;
};

SteppedRun consumed_stepped(bool p_owner_stepped) {
    const Scenario scenario = consumed_lane();
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    const Ref<NetwPrediction> consumer
        = rig.prediction_handle(godot::StringName("P"));
    const Ref<NetwPrediction> owner
        = rig.prediction_handle(godot::StringName("P"), 0);
    REQUIRE(consumer.is_valid());
    REQUIRE(owner.is_valid());
    godot::Node *consumed_body = stepped_body(rig.server(), consumer, true);
    godot::Node *owned_body
        = stepped_body(rig.client(0), owner, p_owner_stepped);
    NetwPredictionEngine *const pool = rig.server()->get_prediction_engine();
    const int64_t slot = pool->slot_of(consumer->get_entity());
    const auto step = [&](int p_frames) {
        for (int frame = 0; frame < p_frames; ++frame) {
            rig.step_frame(1);
        }
    };
    step(STEPPED_FRAMES / 2);
    REQUIRE(
        pool->schedule_of(slot) == int(netw::NetwSimulation::SCHEDULE_STEPPED)
    );
    SteppedRun out;
    const int64_t consumed = fact_of(consumer, "consumed");
    out.confirmed_before = fact_of(owner, "ack_confirmed");
    step(STEPPED_FRAMES / 2);
    out.consumed = fact_of(consumer, "consumed") - consumed;
    out.ack = pool->ack_of(slot);
    out.closed = fact_of(consumer, "journal_closed");
    out.confirmed = fact_of(owner, "ack_confirmed");
    out.corrections = fact_of(owner, "corrections");
    netw::gd::scene_root()->remove_child(consumed_body);
    netw::gd::scene_root()->remove_child(owned_body);
    return out;
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST1 a STEPPED consumer records its history "
    "every tick, so its STEPPED owner hears each input it consumed "
    "acknowledged and corrects nothing on a clean link"
) {
    const SteppedRun run = consumed_stepped(true);
    NETW_CHECK_GE(run.consumed, int64_t(STEPPED_FRAMES / 2 - 1));
    NETW_CHECK_GE(run.confirmed_before, int64_t(0));
    NETW_CHECK_GE(run.confirmed - run.confirmed_before, run.consumed - 1);
    NETW_CHECK_EQ(run.corrections, int64_t(0));
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST2 a STEPPED consumer whose owner "
    "resolved to FRAME closes its journal at the transition it acknowledged, "
    "so the owner hears that acknowledgement"
) {
    const SteppedRun run = consumed_stepped(false);
    NETW_CHECK_GE(run.consumed, int64_t(STEPPED_FRAMES / 2 - 1));
    NETW_CHECK_GE(run.closed, run.ack - 1);
    NETW_CHECK_EQ(run.confirmed, run.closed);
    NETW_CHECK_EQ(run.corrections, int64_t(0));
}

class DriftStepper : public netw::NetwPhysicsStepper {
public:
    godot::LocalVector<godot::Node2D *> bodies;
    godot::Vector2 drift = godot::Vector2(0.5, 0.0);

    bool can_step() override {
        return true;
    }

    void step(const godot::RID &, double) override {
        for (godot::Node2D *body : bodies) {
            body->set_position(body->get_position() + drift);
        }
    }
};

constexpr int DRIFT_SETTLE_FRAMES = 30;
constexpr int DRIFT_OBSERVED_FRAMES = 60;
constexpr double DRIFT_NUDGE = 0.25;
constexpr int64_t DRIFT_PASSES_PER_NUDGE = 3;

Scenario drifting_island() {
    Scenario scenario;
    scenario.label = "stepped-drift";
    scenario.epsilon = 0.01;
    scenario.world.clocked(60, 3)
        .lag_compensated()
        .player(
            EntityDecl()
                .named("P")
                .on_schema("PredictedPose")
                .synced("position")
                .placed_at(godot::Vector2())
                .predicted(0.01)
                .scheduled(netw::Schedule::FRAME),
            0
        )
        .island(
            godot::StringName("P"),
            godot::Vector<godot::StringName>(),
            WorldDecl::INDEPENDENT
        );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(30);
}

godot::Node2D *drifting_body(
    NetwMultiplayer *p_core,
    const Ref<NetwPrediction> &p_handle
) {
    const Ref<NetwEntity> entity = p_handle->get_entity();
    godot::Node2D *body
        = godot::Object::cast_to<godot::Node2D>(entity->get_owner());
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID space = p_core->entity_space_of(entity).space;
    REQUIRE(space.is_valid());
    Ref<DriftStepper> stepper;
    stepper.instantiate();
    stepper->bodies.push_back(body);
    p_core->predict_stepper_install(space, stepper);
    entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
    p_handle->set_reconcile_mode(NetwPrediction::RECONCILE_JOINT);
    return body;
}

struct DriftRun {
    int64_t passes = 0;
    int64_t corrections = 0;
};

DriftRun drifted(bool p_nudged) {
    const Scenario scenario = drifting_island();
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    const Ref<NetwPrediction> consumer
        = rig.prediction_handle(godot::StringName("P"));
    const Ref<NetwPrediction> owner
        = rig.prediction_handle(godot::StringName("P"), 0);
    REQUIRE(consumer.is_valid());
    REQUIRE(owner.is_valid());
    godot::Node2D *authority = drifting_body(rig.server(), consumer);
    godot::Node2D *predicted = drifting_body(rig.client(0), owner);
    NetwPredictionEngine *const pool = rig.client(0)->get_prediction_engine();
    const int64_t slot = pool->slot_of(owner->get_entity());
    const auto frames = [&](int p_frames) {
        for (int frame = 0; frame < p_frames; ++frame) {
            rig.step_frame(1);
        }
    };
    frames(DRIFT_SETTLE_FRAMES);
    REQUIRE(
        pool->schedule_of(slot) == int(netw::NetwSimulation::SCHEDULE_STEPPED)
    );
    REQUIRE(
        pool->reconcile_of(slot) == int(netw::NetwPrediction::RECONCILE_JOINT)
    );
    const int64_t before = joint_passes(pool, slot);
    const int64_t corrected = owner->get_stats()->get_corrections();
    if (p_nudged) {
        authority->set_position(
            authority->get_position() + godot::Vector2(0.0, DRIFT_NUDGE)
        );
    }
    frames(DRIFT_OBSERVED_FRAMES);
    DriftRun out;
    out.passes = joint_passes(pool, slot) - before;
    out.corrections = owner->get_stats()->get_corrections() - corrected;
    netw::gd::scene_root()->remove_child(authority);
    netw::gd::scene_root()->remove_child(predicted);
    return out;
}

int64_t drifted_passes(bool p_nudged) {
    return drifted(p_nudged).passes;
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST7 a JOINT replay records each replayed "
    "row after its space steps, so one difference on a body only its stepper "
    "moves costs a pass or two and the replay then matches the authority"
) {
    SUBCASE("undisturbed, the prediction never replays") {
        NETW_CHECK_EQ(drifted_passes(false), int64_t(0));
    }
    SUBCASE("one nudge, a bounded number of passes") {
        const int64_t passes = drifted_passes(true);
        NETW_CHECK_GE(passes, int64_t(1));
        NETW_CHECK_LE(passes, DRIFT_PASSES_PER_NUDGE);
    }
}

class QueuedStepper : public netw::NetwPhysicsStepper {
public:
    godot::Node2D *body = nullptr;
    double stepped = 0.0;
    int64_t steps = 0;
    int64_t stale = 0;

    bool can_step() override {
        return true;
    }

    void step(const godot::RID &, double) override {
        if (body == nullptr) {
            return;
        }
        if (steps > 0 && body->get_rotation() != stepped) {
            stale += 1;
        }
        stepped = body->get_rotation() + 0.125;
        steps += 1;
    }

    void flush_queries(const godot::RID &, int) override {
        if (body != nullptr) {
            body->set_rotation(stepped);
        }
    }
};

TEST_CASE(
    "[Networked][Predict][Stepped] ST8 a stepper that only steps is read "
    "fresh on the next tick, because the framework flushes the space after "
    "every step"
) {
    const Scenario scenario = consumed_lane();
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    const Ref<NetwPrediction> consumer
        = rig.prediction_handle(godot::StringName("P"));
    REQUIRE(consumer.is_valid());
    const Ref<NetwEntity> entity = consumer->get_entity();
    godot::Node2D *body
        = godot::Object::cast_to<godot::Node2D>(entity->get_owner());
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID space = rig.server()->entity_space_of(entity).space;
    REQUIRE(space.is_valid());
    Ref<QueuedStepper> stepper;
    stepper.instantiate();
    stepper->body = body;
    rig.server()->predict_stepper_install(space, stepper);
    entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );
    for (int frame = 0; frame < STEPPED_FRAMES; ++frame) {
        rig.step_frame(1);
    }
    netw::gd::scene_root()->remove_child(body);
    NETW_CHECK_GE(stepper->steps, int64_t(STEPPED_FRAMES / 2));
    NETW_CHECK_EQ(stepper->stale, int64_t(0));
}

WorldDecl drifting_ball() {
    godot::Vector<godot::StringName> members;
    members.push_back(godot::StringName("Ball"));
    WorldDecl world;
    world.clocked(60, 3)
        .lag_compensated()
        .player(island_player("P", netw::Schedule::FRAME), 0)
        .hosted(island_player("Ball", netw::Schedule::FRAME)
                    .placed_at(godot::Vector2(20.0, 0.0)))
        .island(
            godot::StringName("P"),
            members,
            WorldDecl::INDEPENDENT
        )
        .simulating(godot::StringName("Ball"));
    return world;
}

Ref<DriftStepper> drifting_pair(
    NetwMultiplayer *p_core,
    const Ref<NetwPrediction> &p_player,
    const Ref<NetwPrediction> &p_ball
) {
    Ref<DriftStepper> stepper;
    stepper.instantiate();
    godot::RID space;
    for (const Ref<NetwPrediction> &handle : {p_player, p_ball}) {
        const Ref<NetwEntity> entity = handle->get_entity();
        godot::Node2D *body
            = godot::Object::cast_to<godot::Node2D>(entity->get_owner());
        REQUIRE(body != nullptr);
        netw::gd::scene_root()->add_child(body);
        stepper->bodies.push_back(body);
        space = p_core->entity_space_of(entity).space;
    }
    REQUIRE(space.is_valid());
    p_core->predict_stepper_install(space, stepper);
    for (const Ref<NetwPrediction> &handle : {p_player, p_ball}) {
        handle->get_entity()->get_simulation()->set_schedule(
            netw::NetwSimulation::SCHEDULE_STEPPED
        );
    }
    p_player->set_reconcile_mode(NetwPrediction::RECONCILE_JOINT);
    return stepper;
}

struct BallRun {
    int64_t passes = 0;
    int64_t ball_corrections = 0;
    int64_t ball_installs = 0;
    int64_t owner_corrections = 0;
};

BallRun drifted_ball(bool p_nudged) {
    LoopbackRig rig(1);
    rig.declare_world(drifting_ball());
    const godot::StringName player("P");
    const godot::StringName ball("Ball");
    const Ref<DriftStepper> authority = drifting_pair(
        rig.server(),
        rig.prediction_handle(player),
        rig.prediction_handle(ball)
    );
    const Ref<NetwPrediction> owner = rig.prediction_handle(player, 0);
    const Ref<NetwPrediction> replica = rig.prediction_handle(ball, 0);
    const Ref<DriftStepper> predicted
        = drifting_pair(rig.client(0), owner, replica);
    owner->get_entity()->get_owner()->set("motion", godot::Vector2(1.0, 0.0));
    NetwPredictionEngine *const pool = rig.client(0)->get_prediction_engine();
    const int64_t slot = pool->slot_of(owner->get_entity());
    for (int frame = 0; frame < DRIFT_SETTLE_FRAMES; ++frame) {
        rig.step_frame(1);
    }
    REQUIRE(
        pool->reconcile_of(slot) == int(netw::NetwPrediction::RECONCILE_JOINT)
    );
    const int64_t passes = joint_passes(pool, slot);
    const int64_t ball_corrections = replica->get_stats()->get_corrections();
    const int64_t ball_installs = replica->get_stats()->get_installs();
    const int64_t owner_corrections = owner->get_stats()->get_corrections();
    if (p_nudged) {
        godot::Node2D *served = authority->bodies[1];
        served->set_position(
            served->get_position() + godot::Vector2(0.0, DRIFT_NUDGE)
        );
    }
    for (int frame = 0; frame < DRIFT_OBSERVED_FRAMES; ++frame) {
        rig.step_frame(1);
    }
    BallRun out;
    out.passes = joint_passes(pool, slot) - passes;
    out.ball_corrections
        = replica->get_stats()->get_corrections() - ball_corrections;
    out.ball_installs = replica->get_stats()->get_installs() - ball_installs;
    out.owner_corrections
        = owner->get_stats()->get_corrections() - owner_corrections;
    for (godot::Node2D *body : authority->bodies) {
        netw::gd::scene_root()->remove_child(body);
    }
    for (godot::Node2D *body : predicted->bodies) {
        netw::gd::scene_root()->remove_child(body);
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST9 a stepped simulated member records its "
    "row after the space steps and compares an arrival with it before it "
    "writes, so an arrival that agrees opens no joint replay"
) {
    const BallRun run = drifted_ball(false);
    NETW_CHECK_EQ(run.passes, int64_t(0));
    NETW_CHECK_EQ(run.ball_installs, int64_t(0));
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST10 each entity counts its own "
    "corrections, a joint replay included, and a simulated member counts the "
    "server states written onto it as installs"
) {
    SUBCASE("a nudged owner counts the corrections its replays made") {
        const DriftRun run = drifted(true);
        NETW_CHECK_GE(run.corrections, int64_t(1));
        NETW_CHECK_LE(run.corrections, run.passes);
    }
    SUBCASE("a nudged simulated member counts installs and no correction") {
        const BallRun run = drifted_ball(true);
        NETW_CHECK_GE(run.ball_installs, int64_t(1));
        NETW_CHECK_EQ(run.ball_corrections, int64_t(0));
        NETW_CHECK_EQ(run.owner_corrections, int64_t(0));
    }
}

struct PairedRun {
    int64_t offset_before = -1;
    int64_t offset = -1;
    double client_spacing = 0.0;
    double server_spacing = 0.0;
};

int64_t consume_offset(NetwMultiplayer *p_server, int64_t p_slot) {
    return p_server->clock_get_tick()
        - p_server->get_prediction_engine()->ack_of(p_slot);
}

double spacing(const Ref<DriftStepper> &p_stepper) {
    return p_stepper->bodies[1]->get_position().y
        - p_stepper->bodies[0]->get_position().y;
}

PairedRun paired_by_world_tick(int p_dry_frames) {
    LoopbackRig rig(1);
    rig.declare_world(drifting_ball());
    const godot::StringName player("P");
    const godot::StringName ball("Ball");
    const Ref<NetwPrediction> consumer = rig.prediction_handle(player);
    const Ref<DriftStepper> authority
        = drifting_pair(rig.server(), consumer, rig.prediction_handle(ball));
    const Ref<NetwPrediction> owner = rig.prediction_handle(player, 0);
    const Ref<DriftStepper> predicted
        = drifting_pair(rig.client(0), owner, rig.prediction_handle(ball, 0));
    authority->drift = godot::Vector2(0.0, 0.5);
    predicted->drift = godot::Vector2(0.0, 0.5);
    owner->get_entity()->get_owner()->set("motion", godot::Vector2(1.0, 0.0));
    const int64_t consumed = rig.server()->get_prediction_engine()->slot_of(
        consumer->get_entity()
    );
    for (int frame = 0; frame < DRIFT_SETTLE_FRAMES; ++frame) {
        rig.step_frame(1);
    }
    PairedRun out;
    out.offset_before = consume_offset(rig.server(), consumed);
    rig.hold(-1);
    for (int frame = 0; frame < p_dry_frames; ++frame) {
        rig.step_frame(1);
    }
    rig.release(-1);
    for (int frame = 0; frame < DRIFT_OBSERVED_FRAMES; ++frame) {
        rig.step_frame(1);
    }
    out.offset = consume_offset(rig.server(), consumed);
    out.client_spacing = spacing(predicted);
    out.server_spacing = spacing(authority);
    for (godot::Node2D *body : authority->bodies) {
        netw::gd::scene_root()->remove_child(body);
    }
    for (godot::Node2D *body : predicted->bodies) {
        netw::gd::scene_root()->remove_child(body);
    }
    return out;
}

void check_paired(int p_dry_frames) {
    const PairedRun run = paired_by_world_tick(p_dry_frames);
    REQUIRE(run.offset == run.offset_before + p_dry_frames);
    NETW_CHECK_LT(std::abs(run.client_spacing - run.server_spacing), 0.01);
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST11 a joint replay pairs each member's "
    "row by the world tick it ran at, so a predicting peer keeps the server's "
    "spacing between its own body and a simulated one whatever the consume "
    "offset"
) {
    SUBCASE("consume offset 1") {
        check_paired(0);
    }
    SUBCASE("consume offset 2") {
        check_paired(1);
    }
    SUBCASE("consume offset 3") {
        check_paired(2);
    }
}

struct DrawnRun {
    double body_step = 0.0;
    double drawn_step = 0.0;
    int64_t passes = 0;
};

godot::Node2D *chased_visual(
    NetwMultiplayer *p_core,
    const Ref<NetwPrediction> &p_handle
) {
    const Ref<NetwEntity> entity = p_handle->get_entity();
    const godot::RID rid = entity->get_rid_handle();
    godot::Node *body = entity->get_owner();
    godot::Node2D *visual = memnew(godot::Node2D);
    visual->set_name("Visual");
    body->add_child(visual);
    p_core->display_set_param(
        rid,
        NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT,
        godot::NodePath("Visual")
    );
    p_core->display_set_param(
        rid,
        NetwMultiplayer::DISPLAY_PARAM_LIVE_MODE,
        int64_t(NetwMultiplayer::LIVE_MODE_CHASE)
    );
    Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    netw::Netw::configure_property(body, godot::StringName("position"), false)
        ->interpolate(netw::gd::array_of(spec->lerp()));
    p_core->display_mark_dirty(rid, netw::display::DIRT_RUNTIME);
    p_core->session_flush_deferred();
    return visual;
}

DrawnRun drawn_through_a_pass() {
    const Scenario scenario = drifting_island();
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    const Ref<NetwPrediction> consumer
        = rig.prediction_handle(godot::StringName("P"));
    const Ref<NetwPrediction> owner
        = rig.prediction_handle(godot::StringName("P"), 0);
    REQUIRE(consumer.is_valid());
    REQUIRE(owner.is_valid());
    NetwMultiplayer *const client = rig.client(0);
    godot::Node2D *visual = chased_visual(client, owner);
    godot::Node2D *authority = drifting_body(rig.server(), consumer);
    godot::Node2D *predicted = drifting_body(client, owner);
    NetwPredictionEngine *const pool = client->get_prediction_engine();
    const int64_t slot = pool->slot_of(owner->get_entity());
    netw::display::Runtime *const drawing
        = netw::NetwNativeTests::display_runtime_of(
            client,
            owner->get_entity()->get_rid_handle()
        );
    REQUIRE(drawing != nullptr);
    const auto frames = [&](int p_frames, DrawnRun *r_run) {
        for (int frame = 0; frame < p_frames; ++frame) {
            const double body_before = predicted->get_global_position().y;
            const double drawn_before = visual->get_global_position().y;
            rig.step_frame(1);
            netw::NetwNativeTests::display_pump_runtime(
                client,
                drawing,
                netw::display::capture_timing(
                    &client->clock_engine(),
                    1.0 / 60.0
                )
            );
            if (r_run == nullptr) {
                continue;
            }
            r_run->body_step = std::max(
                r_run->body_step,
                std::abs(predicted->get_global_position().y - body_before)
            );
            r_run->drawn_step = std::max(
                r_run->drawn_step,
                std::abs(visual->get_global_position().y - drawn_before)
            );
        }
    };
    frames(DRIFT_SETTLE_FRAMES, nullptr);
    REQUIRE(
        pool->reconcile_of(slot) == int(netw::NetwPrediction::RECONCILE_JOINT)
    );
    const int64_t before = joint_passes(pool, slot);
    authority->set_position(
        authority->get_position() + godot::Vector2(0.0, DRIFT_NUDGE)
    );
    DrawnRun out;
    frames(DRIFT_OBSERVED_FRAMES, &out);
    out.passes = joint_passes(pool, slot) - before;
    netw::gd::scene_root()->remove_child(authority);
    netw::gd::scene_root()->remove_child(predicted);
    return out;
}

TEST_CASE(
    "[Networked][Predict][Stepped] ST12 a joint replay's move of the present "
    "body is absorbed into a chase display, so the drawn body glides where "
    "the body jumps"
) {
    const DrawnRun run = drawn_through_a_pass();
    REQUIRE(run.passes >= 1);
    REQUIRE(run.body_step >= DRIFT_NUDGE * 0.9);
    NETW_CHECK_LT(run.drawn_step, DRIFT_NUDGE * 0.5);
}

} // namespace TestNetwPredictSteppedLaws

#endif
