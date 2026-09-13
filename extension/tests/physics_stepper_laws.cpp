#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"
#include "support/stepper_recorder.h"

#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/predict/engine.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwPhysicsStepperLaws {

using namespace netw_test;
using godot::Ref;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;

EntityDecl island_player(const char *p_name) {
    return EntityDecl()
        .named(p_name)
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(godot::Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario stepped_island_lane() {
    Scenario scenario;
    scenario.label = "stepped-island";
    scenario.epsilon = 0.01;
    godot::Vector<godot::StringName> members;
    members.push_back(godot::StringName("Q"));
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(island_player("P"), 0)
        .player(island_player("Q"), 1)
        .island(godot::StringName("P"), members)
        .simulating(godot::StringName("Q"));
    scenario.clients = 2;
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", godot::Vector2(1.0, 0.0));
    return scenario.until(60);
}

Scenario independent_lane() {
    Scenario scenario;
    scenario.label = "stepped-independent";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(island_player("P"), 0)
        .player(island_player("Q"), 1);
    scenario.clients = 2;
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", godot::Vector2(1.0, 0.0));
    return scenario.until(60);
}

bool space_is_active(const NetwMultiplayer::EntitySpace &p_held) {
    if (p_held.dimension == 3) {
        return godot::PhysicsServer3D::get_singleton()->space_is_active(
            p_held.space
        );
    }
    return godot::PhysicsServer2D::get_singleton()->space_is_active(
        p_held.space
    );
}

struct SteppedOwner {
    Ref<NetwEntity> entity;
    Ref<RecordingStepper> stepper;
    NetwMultiplayer::EntitySpace held;
    godot::RID space;
    int64_t slot = -1;

    void release() {
        if (entity.is_valid() && entity->get_owner() != nullptr) {
            netw::gd::scene_root()->remove_child(entity->get_owner());
        }
    }
};

struct SteppedSpace {
    Ref<RecordingStepper> stepper;
    NetwMultiplayer::EntitySpace held;
    godot::LocalVector<Ref<NetwEntity>> members;

    void release() {
        for (uint32_t at = 0; at < members.size(); ++at) {
            godot::Node *owner = members[at]->get_owner();
            if (owner != nullptr && owner->is_inside_tree()) {
                netw::gd::scene_root()->remove_child(owner);
            }
        }
    }
};

SteppedSpace step_one_space(NetwMultiplayer *p_core, int p_members) {
    SteppedSpace out;
    NetwPredictionEngine *const pool = p_core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const godot::TypedArray<godot::Object> seated
        = p_core->predict_stepped_entities();
    for (int at = 0; at < seated.size(); ++at) {
        const Ref<NetwEntity> entity = seated[at];
        if (entity.is_null() || entity->get_owner() == nullptr
            || pool->slot_of(entity) < 0) {
            continue;
        }
        if (!entity->get_owner()->is_inside_tree()) {
            netw::gd::scene_root()->add_child(entity->get_owner());
        }
        const NetwMultiplayer::EntitySpace held
            = p_core->entity_space_of(entity);
        if (!held.space.is_valid()) {
            continue;
        }
        if (out.stepper.is_null()) {
            out.held = held;
            out.stepper.instantiate();
            p_core->predict_stepper_install(out.held.space, out.stepper);
        } else if (held.space != out.held.space) {
            continue;
        }
        entity->get_prediction()->set_schedule(NetwPredict::SCHEDULE_STEPPED);
        out.members.push_back(entity);
        if (int(out.members.size()) >= p_members) {
            break;
        }
    }
    return out;
}

SteppedOwner step_the_island(NetwMultiplayer *p_core) {
    SteppedOwner out;
    NetwPredictionEngine *const pool = p_core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const godot::TypedArray<godot::Object> held
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
    if (out.slot < 0) {
        return out;
    }
    netw::gd::scene_root()->add_child(out.entity->get_owner());
    out.held = p_core->entity_space_of(out.entity);
    out.space = out.held.space;
    out.stepper.instantiate();
    p_core->predict_stepper_install(out.space, out.stepper);
    out.entity->get_prediction()->set_schedule(NetwPredict::SCHEDULE_STEPPED);
    return out;
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST1 a space the framework steps is "
    "held inactive while a member stands in it and is released the moment its "
    "stepper is uninstalled, because two things stepping one space is two "
    "simulations"
) {
    const Scenario scenario = stepped_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    SteppedOwner owner = step_the_island(predictor);
    REQUIRE(owner.slot >= 0);

    rig.step_ticks(2);
    CHECK_FALSE(space_is_active(owner.held));

    predictor->predict_stepper_install(owner.space, Ref<RecordingStepper>());
    CHECK(space_is_active(owner.held));
    owner.release();
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST2 the forward path steps a "
    "stepped space once per network tick and snapshots the tick it just "
    "integrated, so a space integrates on the network's clock rather than the "
    "frame's"
) {
    const Scenario scenario = independent_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    SteppedSpace one = step_one_space(predictor, 1);
    const bool one_stands_in_it = one.members.size() == 1;
    REQUIRE(one_stands_in_it);

    rig.step_ticks(1);
    one.stepper->forget();
    rig.step_ticks(4);

    NETW_CHECK_EQ(one.stepper->count_of(RecordingStepper::STEP), 4);
    CHECK(one.stepper->every_step_is_snapshotted());
    one.release();
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST3 a joint replay restores the "
    "space once at its basis and then steps it once for every unacknowledged "
    "tick, so the hook runs per pass rather than per member"
) {
    const Scenario scenario = stepped_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    SteppedOwner owner = step_the_island(predictor);
    REQUIRE(owner.slot >= 0);

    rig.step_ticks(2);
    const int64_t passes_before = pool->joint_stats(
        owner.slot
    )[NetwPredictionEngine::STAT_JOINT_PASSES];
    owner.stepper->forget();
    rig.step_ticks(4);
    const int64_t passes
        = pool->joint_stats(owner.slot)[NetwPredictionEngine::STAT_JOINT_PASSES]
        - passes_before;
    REQUIRE(passes > 0);

    NETW_CHECK_EQ(
        owner.stepper->count_of(RecordingStepper::RESTORE),
        int(passes)
    );
    NETW_CHECK_GT(
        owner.stepper->count_of(RecordingStepper::STEP),
        owner.stepper->count_of(RecordingStepper::RESTORE)
    );
    NETW_CHECK_EQ(
        owner.stepper->count_of(RecordingStepper::SNAPSHOT),
        owner.stepper->count_of(RecordingStepper::STEP)
    );
    owner.release();
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST4 a replay restores before it "
    "steps and snapshots after, so the first call of a pass is the restore at "
    "its basis and every step that follows carries a later tick"
) {
    const Scenario scenario = stepped_island_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    SteppedOwner owner = step_the_island(predictor);
    REQUIRE(owner.slot >= 0);

    rig.step_ticks(2);
    owner.stepper->forget();
    rig.step_ticks(4);

    const godot::LocalVector<RecordingStepper::Row> &rows
        = owner.stepper->recorded();
    REQUIRE(!rows.is_empty());
    int64_t basis = -1;
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at].call == RecordingStepper::RESTORE) {
            basis = rows[at].tick;
            continue;
        }
        if (rows[at].call == RecordingStepper::SNAPSHOT && basis >= 0) {
            NETW_CHECK_GT(rows[at].tick, basis);
        }
    }
    NETW_CHECK_GE(basis, 0);
    owner.release();
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST5 one space is stepped once per "
    "tick however many members stand in it, because a step belongs to the "
    "space and not to the member that asked for it"
) {
    const Scenario scenario = independent_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.server();
    REQUIRE(predictor != nullptr);
    SteppedSpace one = step_one_space(predictor, 2);
    const bool two_stand_in_it = one.members.size() == 2;
    REQUIRE(two_stand_in_it);

    rig.step_ticks(1);
    one.stepper->forget();
    rig.step_ticks(3);
    NETW_CHECK_EQ(one.stepper->count_of(RecordingStepper::STEP), 3);
    one.release();
}

TEST_CASE(
    "[Networked][Predict][Stepper] ST6 the forward path DRIVES every stepped "
    "member once per network tick before it steps their space, because a "
    "space stepped around a member nothing drove integrates a world the game "
    "never authored"
) {
    const Scenario scenario = independent_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    REQUIRE(predictor != nullptr);
    NetwPredictionEngine *const pool = predictor->get_prediction_engine();
    REQUIRE(pool != nullptr);
    SteppedSpace one = step_one_space(predictor, 1);
    const bool one_stands_in_it = one.members.size() == 1;
    REQUIRE(one_stands_in_it);

    const int64_t slot = pool->slot_of(one.members[0]);
    REQUIRE(slot >= 0);
    rig.step_ticks(1);
    const int64_t driven_before = pool->last_driven_input_tick_of(slot);
    one.stepper->forget();
    rig.step_ticks(4);

    NETW_CHECK_EQ(one.stepper->count_of(RecordingStepper::STEP), 4);
    NETW_CHECK_GE(pool->last_driven_input_tick_of(slot) - driven_before, 3);
    one.release();
}

} // namespace TestNetwPhysicsStepperLaws

#endif
