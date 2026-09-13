#include "support/netw_test.h"

#include "support/netw_call_log.h"
#include "support/netw_cells.h"

#include "godot/callable.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/prediction_handle.hpp"

namespace TestNetwSessionPredictDeclareLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;
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
    PLANT_A_MEMBER_THE_ISLAND_NEVER_TOOK,
};

struct PredictScenario {
    String label;
    int64_t schedule = NetwPredict::SCHEDULE_FRAME;
    bool joins_an_island = false;
    bool approximates = false;
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

PredictScenario an_island_of_two() {
    PredictScenario scenario;
    scenario.label = "an-island-of-two";
    scenario.joins_an_island = true;
    return scenario;
}

PredictScenario an_approximating_island() {
    PredictScenario scenario;
    scenario.label = "an-approximating-island";
    scenario.joins_an_island = true;
    scenario.approximates = true;
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
    bool simulate_is_the_one_installed = false;
    Error island_verdict = FAILED;
    bool island_holds_the_other = false;
    bool island_approximates = false;

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
        if (declared.joins_an_island) {
            session->predict_declare(other_entity);
        }
        engine_after_declare = session->predict_engine_seated(entity);

        session->predict_set_param(
            planted == PLANT_A_PARAM_WRITTEN_TO_ANOTHER_ENTITY ? other_entity
                                                               : entity,
            NetwMultiplayer::PREDICT_PARAM_SCHEDULE,
            declared.schedule
        );
        schedule_read = session->predict_get_param(
            entity,
            NetwMultiplayer::PREDICT_PARAM_SCHEDULE
        );

        const Callable sensor = log.answering("sensor", 17);
        const Callable simulate = log.callable("simulate");
        if (declared.installs_callbacks) {
            session->predict_set_sensor_callback(
                entity,
                StringName(SENSOR_KEY),
                sensor
            );
            session->predict_set_simulate_callback(entity, simulate);
        }

        if (declared.joins_an_island) {
            island_verdict = planted == PLANT_A_MEMBER_THE_ISLAND_NEVER_TOOK
                ? OK
                : session->predict_island_add(entity, other_entity);
            session->predict_island_set_param(
                entity,
                NetwMultiplayer::ISLAND_PARAM_APPROXIMATE,
                declared.approximates
            );
        } else {
            island_verdict = OK;
        }

        const Ref<NetwPredictionHandle> handle
            = session->prediction_handle(entity);
        if (handle.is_valid()) {
            sensor_is_the_one_installed = Variant(handle->get_sensors().get(
                                              StringName(SENSOR_KEY),
                                              Variant()
                                          ))
                == Variant(sensor);
            simulate_is_the_one_installed = handle->get_simulate() == simulate;
            const Ref<netw::NetwPredictIsland> island = handle->get_island();
            if (island.is_valid()) {
                island_holds_the_other = island->has_member(other);
                island_approximates = island->get_approximate();
            }
        }

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

    bool simulate_kept() const {
        return simulate_is_the_one_installed;
    }

    Error island() const {
        return island_verdict;
    }

    bool island_member() const {
        return island_holds_the_other;
    }

    bool approximates() const {
        return island_approximates;
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
    if (!p_run.simulate_kept()) {
        return law_broken("the handle holds a step nobody installed");
    }
    return law_held();
}

LawVerdict law_islanded(const PredictRun &p_run) {
    if (p_run.island() != OK) {
        return law_broken("joining an island answered %d", int(p_run.island()));
    }
    if (!p_run.scenario().joins_an_island) {
        return law_held();
    }
    if (!p_run.island_member()) {
        return law_broken("the island does not hold the entity it was given");
    }
    if (p_run.approximates() != p_run.scenario().approximates) {
        return law_broken(
            "the island approximates %d against the %d declared",
            int(p_run.approximates()),
            int(p_run.scenario().approximates)
        );
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

const PredictLaw L_ISLANDED = {
    "islanded",
    "an island named by RID holds the members and the rule it was given",
    &law_islanded,
};

const PredictLaw LAWS[]
    = {L_DECLARED, L_UNDECLARED, L_PARAMETERISED, L_ADDRESSED, L_ISLANDED};

TEST_CASE("[Networked][Session][SceneTree] the flat prediction laws hold") {
    const PredictScenario CORPUS[] = {
        a_bare_declaration(),
        a_declaration_with_callbacks(),
        an_island_of_two(),
        an_approximating_island(),
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
    "[Networked][Session][SceneTree] a member the island never took reds "
    "islanded"
) {
    const PredictScenario scenario = an_island_of_two();
    const PredictRun run(scenario, PLANT_A_MEMBER_THE_ISLAND_NEVER_TOOK);
    NETW_CELL(L_ISLANDED, scenario);
    NETW_LAW_BREAKS(L_ISLANDED, run);
}

} // namespace TestNetwSessionPredictDeclareLaws
