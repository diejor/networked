#include "support/netw_test.h"

#include "support/netw_call_log.h"
#include "support/netw_cells.h"

#include "godot/callable.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/predict/engine.hpp"

namespace TestNetwSessionPredictDeclareLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;
using netw::NetwSimulationHandle;
using netw_test::CallLog;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *SENSOR_KEY = "ground";

enum Plant {
    PLANT_NONE,
    PLANT_A_DECLARATION_THAT_NEVER_HAPPENED,
    PLANT_A_PARAM_WRITTEN_TO_ANOTHER_ENTITY,
    PLANT_AN_UNDECLARE_THAT_NEVER_HAPPENED,
    PLANT_A_MEMBER_THE_SELECTION_NEVER_TOOK,
};

struct PredictScenario {
    String label;
    int64_t schedule = NetwSimulationHandle::SCHEDULE_FRAME;
    bool selects_another = false;
    bool installs_callbacks = false;
};

PredictScenario a_bare_declaration() {
    PredictScenario scenario;
    scenario.label = "a-bare-declaration";
    return scenario;
}

PredictScenario a_declaration_with_callbacks() {
    PredictScenario scenario;
    scenario.label = "a-declaration-with-callbacks";
    scenario.installs_callbacks = true;
    return scenario;
}

PredictScenario a_selection_of_two() {
    PredictScenario scenario;
    scenario.label = "a-selection-of-two";
    scenario.selects_another = true;
    return scenario;
}

PredictScenario a_selection_with_callbacks() {
    PredictScenario scenario;
    scenario.label = "a-selection-with-callbacks";
    scenario.selects_another = true;
    scenario.installs_callbacks = true;
    return scenario;
}

class PredictRun {
    PredictScenario declared;
    Plant planted = PLANT_NONE;
    Error declare_verdict = FAILED;
    bool engine_after_declare = false;
    bool engine_after_undeclare = true;
    Variant schedule_read;
    bool sensor_is_the_one_installed = false;
    bool step_is_the_one_installed = false;
    bool selection_holds_the_other = false;

public:
    PredictRun(const PredictScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        REQUIRE(session->lagcomp_initialize(8, 12) == OK);
        CallLog log;

        Node3D *body = memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        Node3D *other_body = memnew(Node3D);
        netw::gd::scene_root()->add_child(other_body);
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        const Ref<NetwEntity> other = NetwEntity::ensure(other_body);
        const RID entity = session->entity_of(body);
        const RID other_entity = session->entity_of(other_body);

        declare_verdict = planted == PLANT_A_DECLARATION_THAT_NEVER_HAPPENED
            ? OK
            : session->predict_declare(entity);
        if (declared.selects_another) {
            session->predict_declare(other_entity);
        }
        engine_after_declare = session->predict_engine_seated(entity);

        session->simulation_set_param(
            planted == PLANT_A_PARAM_WRITTEN_TO_ANOTHER_ENTITY ? other_entity
                                                               : entity,
            NetwMultiplayer::SIMULATION_PARAM_SCHEDULE,
            declared.schedule
        );
        schedule_read = session->simulation_get_param(
            entity,
            NetwMultiplayer::SIMULATION_PARAM_SCHEDULE
        );

        const Callable sensor = log.answering("sensor", 17);
        const Callable step = log.callable("step");
        if (declared.installs_callbacks) {
            session->predict_set_sensor_callback(
                entity,
                StringName(SENSOR_KEY),
                sensor
            );
            session->simulation_set_step_callback(entity, step);
        }

        const Ref<NetwSimulationHandle> simulation
            = session->simulation_handle(entity);
        if (declared.selects_another
            && planted != PLANT_A_MEMBER_THE_SELECTION_NEVER_TOOK) {
            simulation->simulate(other);
        }

        const Ref<NetwPredictionHandle> handle
            = session->prediction_handle(entity);
        if (handle.is_valid()) {
            sensor_is_the_one_installed = Variant(handle->get_sensors().get(
                                              StringName(SENSOR_KEY),
                                              Variant()
                                          ))
                == Variant(sensor);
            step_is_the_one_installed = handle->get_simulate() == step;
        }
        const netw::sim::Named *named
            = simulation->selection_choice().named_of(other_entity);
        selection_holds_the_other
            = named != nullptr && named->pick == netw::sim::Pick::CHOSEN;

        if (planted != PLANT_AN_UNDECLARE_THAT_NEVER_HAPPENED) {
            session->predict_undeclare(entity);
        }
        engine_after_undeclare = session->predict_engine_seated(entity);

        session->clear_session_state();
        other_body->queue_free();
        body->queue_free();
    }

    const PredictScenario &scenario() const {
        return declared;
    }

    Error declaration() const {
        return declare_verdict;
    }

    bool engine_stood_up() const {
        return engine_after_declare;
    }

    bool engine_stood_down() const {
        return !engine_after_undeclare;
    }

    const Variant &schedule() const {
        return schedule_read;
    }

    bool sensor_kept() const {
        return sensor_is_the_one_installed;
    }

    bool step_kept() const {
        return step_is_the_one_installed;
    }

    bool selection_member() const {
        return selection_holds_the_other;
    }
};

typedef LawRowFor<PredictRun> PredictLaw;

LawVerdict law_declared(const PredictRun &p_run) {
    if (p_run.declaration() != OK) {
        return law_broken(
            "declaring prediction answered %d",
            int(p_run.declaration())
        );
    }
    if (!p_run.engine_stood_up()) {
        return law_broken("a declared entity has no engine to run it");
    }
    return law_held();
}

LawVerdict law_undeclared(const PredictRun &p_run) {
    if (!p_run.engine_stood_down()) {
        return law_broken("an undeclared entity kept its engine");
    }
    return law_held();
}

LawVerdict law_parameterised(const PredictRun &p_run) {
    if (p_run.schedule().get_type() != Variant::INT) {
        return law_broken(
            "the schedule reads back as a %d rather than an int",
            int(p_run.schedule().get_type())
        );
    }
    if (int64_t(p_run.schedule()) != p_run.scenario().schedule) {
        return law_broken(
            "the schedule reads %d against the %d that was written",
            int(int64_t(p_run.schedule())),
            int(p_run.scenario().schedule)
        );
    }
    return law_held();
}

LawVerdict law_addressed(const PredictRun &p_run) {
    if (!p_run.scenario().installs_callbacks) {
        return law_held();
    }
    if (!p_run.sensor_kept()) {
        return law_broken("the handle holds a sensor nobody installed");
    }
    if (!p_run.step_kept()) {
        return law_broken("the handle holds a step nobody installed");
    }
    return law_held();
}

LawVerdict law_selecting(const PredictRun &p_run) {
    if (!p_run.scenario().selects_another) {
        return law_held();
    }
    if (!p_run.selection_member()) {
        return law_broken("the selection does not hold the entity it named");
    }
    return law_held();
}

const PredictLaw L_DECLARED = {
    "declared",
    "a declared entity has an engine addressed by its own RID",
    &law_declared,
};

const PredictLaw L_UNDECLARED = {
    "undeclared",
    "undeclaring takes the engine with it",
    &law_undeclared,
};

const PredictLaw L_PARAMETERISED = {
    "parameterised",
    "a parameter written by RID reads back through the same address",
    &law_parameterised,
};

const PredictLaw L_ADDRESSED = {
    "addressed",
    "a callback installed by RID is the one the handle holds",
    &law_addressed,
};

const PredictLaw L_SELECTING = {
    "selecting",
    "a selection made on the handle of an entity named by RID holds the "
    "entity it was given",
    &law_selecting,
};

const PredictLaw LAWS[]
    = {L_DECLARED, L_UNDECLARED, L_PARAMETERISED, L_ADDRESSED, L_SELECTING};

TEST_CASE("[Networked][Session][SceneTree] the flat prediction laws hold") {
    const PredictScenario CORPUS[] = {
        a_bare_declaration(),
        a_declaration_with_callbacks(),
        a_selection_of_two(),
        a_selection_with_callbacks(),
    };
    for (const PredictScenario &scenario : CORPUS) {
        const PredictRun run(scenario);
        for (const PredictLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] an entity nobody declared reds "
    "declared"
) {
    const PredictScenario scenario = a_bare_declaration();
    const PredictRun run(scenario, PLANT_A_DECLARATION_THAT_NEVER_HAPPENED);
    NETW_CELL(L_DECLARED, scenario);
    NETW_LAW_BREAKS(L_DECLARED, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a parameter written to another "
    "entity reds parameterised"
) {
    const PredictScenario scenario = a_bare_declaration();
    const PredictRun run(scenario, PLANT_A_PARAM_WRITTEN_TO_ANOTHER_ENTITY);
    NETW_CELL(L_PARAMETERISED, scenario);
    NETW_LAW_BREAKS(L_PARAMETERISED, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a declaration nobody withdrew reds "
    "undeclared"
) {
    const PredictScenario scenario = a_bare_declaration();
    const PredictRun run(scenario, PLANT_AN_UNDECLARE_THAT_NEVER_HAPPENED);
    NETW_CELL(L_UNDECLARED, scenario);
    NETW_LAW_BREAKS(L_UNDECLARED, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a member the selection never took reds "
    "selecting"
) {
    const PredictScenario scenario = a_selection_of_two();
    const PredictRun run(scenario, PLANT_A_MEMBER_THE_SELECTION_NEVER_TOOK);
    NETW_CELL(L_SELECTING, scenario);
    NETW_LAW_BREAKS(L_SELECTING, run);
}

struct ReplayScenario {
    String label;
    bool rigid = false;
    int64_t schedule = NetwSimulationHandle::SCHEDULE_FRAME;
    bool integrates = true;
};

ReplayScenario a_frame_solver_body() {
    return {
        "a-frame-solver-body",
        true,
        NetwSimulationHandle::SCHEDULE_FRAME,
        false,
    };
}

ReplayScenario a_stepped_solver_body() {
    return {
        "a-stepped-solver-body",
        true,
        NetwSimulationHandle::SCHEDULE_STEPPED,
        true,
    };
}

ReplayScenario a_frame_plain_body() {
    return {
        "a-frame-plain-body",
        false,
        NetwSimulationHandle::SCHEDULE_FRAME,
        true,
    };
}

ReplayScenario a_tick_solver_body() {
    return {
        "a-tick-solver-body",
        true,
        NetwSimulationHandle::SCHEDULE_TICK,
        true,
    };
}

class ReplayRun {
    ReplayScenario declared;
    NetwPredict::RecoveryPolicy policy = NetwPredict::RECOVERY_POLICY_OBSERVE;
    int correction = int(netw::CorrectionMode::AUTO);

public:
    explicit ReplayRun(const ReplayScenario &p_scenario)
        : declared(p_scenario) {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        REQUIRE(session->lagcomp_initialize(8, 12) == OK);

        Node3D *body = declared.rigid ? memnew(RigidBody3D) : memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        const RID entity = session->entity_of(body);
        REQUIRE(session->predict_declare(entity) == OK);
        session->predict_set_param(
            entity,
            NetwMultiplayer::PREDICT_PARAM_ARCHETYPE,
            NetwPredict::ARCHETYPE_SOLVER_BODY
        );
        session->simulation_set_param(
            entity,
            NetwMultiplayer::SIMULATION_PARAM_SCHEDULE,
            declared.schedule
        );
        session->predict_set_param(
            entity,
            NetwMultiplayer::PREDICT_PARAM_RECOVERY_POLICY,
            NetwPredict::RECOVERY_POLICY_REBASE_REPLAY
        );

        const Ref<NetwPredictionHandle> handle
            = session->prediction_handle(entity);
        REQUIRE(handle.is_valid());
        policy = handle->resolved_recovery_policy();
        netw::NetwPredictionEngine *pool = session->get_prediction_engine();
        REQUIRE(pool != nullptr);
        const int64_t slot = pool->slot_register(wrapper);
        REQUIRE(slot >= 0);
        const int declared
            = netw::NetwPredictionEngine::correction_for_recovery_policy(
                handle->get_recovery_policy()
            );
        pool->settle_correction(slot, declared);
        correction = pool->settle_correction(slot, declared);

        session->predict_undeclare(entity);
        session->clear_session_state();
        body->queue_free();
    }

    const ReplayScenario &scenario() const {
        return declared;
    }

    NetwPredict::RecoveryPolicy recovery() const {
        return policy;
    }

    int mode() const {
        return correction;
    }
};

typedef LawRowFor<ReplayRun> ReplayLaw;

LawVerdict law_integrated(const ReplayRun &p_run) {
    const bool replays
        = p_run.recovery() == NetwPredict::RECOVERY_POLICY_REBASE_REPLAY;
    const bool corrects_by_replay
        = p_run.mode() == int(netw::CorrectionMode::REPLAY);
    if (replays != corrects_by_replay) {
        return law_broken(
            "the policy resolves %d and the correction resolves %d",
            int(p_run.recovery()),
            int(p_run.mode())
        );
    }
    if (p_run.scenario().integrates && !replays) {
        return law_broken(
            "a body whose replay integrates resolves %d",
            int(p_run.recovery())
        );
    }
    if (!p_run.scenario().integrates
        && p_run.recovery() != NetwPredict::RECOVERY_POLICY_REBASE_RECOVER) {
        return law_broken(
            "a FRAME solver body declaring REPLAY resolves %d",
            int(p_run.recovery())
        );
    }
    return law_held();
}

const ReplayLaw L_INTEGRATED = {
    "integrated",
    "a body whose step applies forces is never replayed without a physics "
    "step, so a FRAME solver body declaring REPLAY resolves RECOVER",
    &law_integrated,
};

TEST_CASE(
    "[Networked][Session][SceneTree] a replay the space cannot integrate "
    "resolves to recovery"
) {
    const ReplayScenario CORPUS[] = {
        a_frame_solver_body(),
        a_stepped_solver_body(),
        a_frame_plain_body(),
        a_tick_solver_body(),
    };
    for (const ReplayScenario &scenario : CORPUS) {
        const ReplayRun run(scenario);
        NETW_CELL(L_INTEGRATED, scenario);
        NETW_LAW_HOLDS(L_INTEGRATED, run);
    }
}

void check_auto_resolution(bool p_rigid) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    REQUIRE(session->lagcomp_initialize(8, 12) == OK);

    Node3D *body = p_rigid ? memnew(RigidBody3D) : memnew(Node3D);
    netw::gd::scene_root()->add_child(body);
    const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
    const RID entity = session->entity_of(body);
    REQUIRE(session->predict_declare(entity) == OK);
    const Ref<NetwPredictionHandle> handle = session->prediction_handle(entity);
    REQUIRE(handle.is_valid());

    const int expected = p_rigid ? NetwPredict::RECOVERY_POLICY_REBASE_RECOVER
                                 : NetwPredict::RECOVERY_POLICY_REBASE_REPLAY;
    const int expected_correction
        = netw::NetwPredictionEngine::correction_for_recovery_policy(expected);
    NETW_CHECK_EQ(
        int(handle->get_recovery_policy()),
        int(NetwPredict::RECOVERY_POLICY_AUTO)
    );
    NETW_CHECK_EQ(int(handle->resolved_recovery_policy()), expected);

    netw::NetwPredictionEngine *pool = session->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const int64_t slot = pool->slot_register(wrapper);
    REQUIRE(slot >= 0);
    const int automatic = int(netw::CorrectionMode::AUTO);
    NETW_CHECK_EQ(int(handle->resolved_recovery_policy()), expected);
    NETW_CHECK_EQ(
        pool->resolve_correction(slot, automatic),
        expected_correction
    );

    pool->unbind_owner(slot);
    NETW_CHECK_EQ(int(handle->resolved_recovery_policy()), expected);
    NETW_CHECK_EQ(
        pool->resolve_correction(slot, automatic),
        expected_correction
    );

    session->predict_undeclare(entity);
    session->clear_session_state();
    body->queue_free();
}

TEST_CASE(
    "[Networked][Session][SceneTree] an automatic recovery policy resolves "
    "from the body, and the handle and the engine resolve it alike"
) {
    check_auto_resolution(true);
    check_auto_resolution(false);
}

} // namespace TestNetwSessionPredictDeclareLaws
