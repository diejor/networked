#include "support/netw_test.h"

#include "support/installable_stepper.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/predict/engine.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwPredictSteppedLaws {

using namespace netw_test;
using godot::Ref;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;

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
    p_owner.entity->get_prediction()->set_schedule(
        NetwPredict::SCHEDULE_STEPPED
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

    const Ref<NetwPredictionHandle> handle = owner.entity->get_prediction();
    REQUIRE(handle.is_valid());
    NETW_CHECK_EQ(
        handle->get_reconcile_mode(),
        int(netw::NetwPredict::RECONCILE_JOINT)
    );

    SUBCASE("TICK re-admits within a tick") {
        handle->set_reconcile_mode(
            int(netw::NetwPredict::RECONCILE_INDEPENDENT)
        );
        rig.step_ticks(4);
        NETW_CHECK_EQ(
            handle->get_reconcile_mode(),
            int(netw::NetwPredict::RECONCILE_JOINT)
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
            int(netw::NetwPredict::RECONCILE_JOINT)
        );

        handle->set_reconcile_mode(
            int(netw::NetwPredict::RECONCILE_INDEPENDENT)
        );
        rig.step_ticks(4);
        NETW_CHECK_EQ(
            handle->get_reconcile_mode(),
            int(netw::NetwPredict::RECONCILE_JOINT)
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
    "[Networked][Predict][Stepped] a stock engine refuses STEPPED loudly, so "
    "a member whose space holds no stepper resolves to SCHEDULE_FRAME, says "
    "so once, and keeps the declaration the game made"
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
    NETW_CHECK_EQ(
        pool->schedule_of(owner.slot),
        int(netw::NetwPredict::SCHEDULE_TICK)
    );

    const Ref<NetwPredictionHandle> handle = owner.entity->get_prediction();
    handle->set_schedule(NetwPredict::SCHEDULE_STEPPED);
    rig.step_ticks(4);

    CHECK_FALSE(pool->slot_has_stepper(owner.slot));
    NETW_CHECK_EQ(
        pool->schedule_of(owner.slot),
        int(netw::NetwPredict::SCHEDULE_FRAME)
    );
    CHECK(pool->stepper_absence_reported_of(owner.slot));
    NETW_CHECK_EQ(
        handle->get_schedule(),
        int(netw::NetwPredict::SCHEDULE_STEPPED)
    );

    const int64_t driven_before = pool->last_driven_input_tick_of(owner.slot);
    rig.step_frame(4);
    NETW_CHECK_GE(
        pool->last_driven_input_tick_of(owner.slot) - driven_before,
        3
    );
}

} // namespace TestNetwPredictSteppedLaws

#endif
