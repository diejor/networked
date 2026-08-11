#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwDeclaredDeterminismLaws {

using namespace netw_test;

EntityDecl reproducible_player() {
    return EntityDecl()
        .named("P")
        .on_schema("ReproduciblePose")
        .synced("position")
        .placed_at(godot::Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario reproducible_lane(const char *p_label) {
    Scenario scenario;
    scenario.label = p_label;
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        reproducible_player(),
        0
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario;
}

// Nothing impairs the link and nothing perturbs the body, so the run is the
// arithmetic alone. A disagreement here is the floor itself moving.
Scenario exact_lane() {
    return reproducible_lane("exact-lane").until(100);
}

// A seeded impairment and a disturbance authority sees, so the run reaches
// correction, replay and recovery. A disagreement here is a decision that
// read something outside the scenario.
Scenario impaired_lane() {
    Scenario scenario = reproducible_lane("impaired-lane");
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.perturb(30, "P", godot::Vector2(60.0, -40.0));
    return scenario.until(120);
}

LawVerdict law_a_run_reproduces_itself(const ScenarioRun &p_run) {
    if (!p_run.witnessed_a_replica()) {
        return law_broken("the run was never handed a replica to agree with");
    }
    if (p_run.digest() != p_run.replica_digest()) {
        return law_broken(
            "two runs of one scenario reduced to %lld and %lld",
            (long long)p_run.digest(),
            (long long)p_run.replica_digest()
        );
    }
    return law_held();
}

const LawRow L_REPRODUCIBLE = {
    "L-REPRODUCIBLE",
    "two runs of one scenario in one process reduce to the same evidence, "
    "counter for counter and bit for bit",
    law_a_run_reproduces_itself,
};

// Drives the scenario twice on two rigs and hands the first run the second's
// digest. The replica takes the plant, because what a plant has to change for
// this law to see it is the SECOND run rather than both.
ScenarioRun run_against_replica(const Scenario &p_scenario, Plant p_plant) {
    LoopbackRig first(p_scenario.clients);
    ScenarioRun run = ScenarioRun::session(first, p_scenario);
    LoopbackRig second(p_scenario.clients);
    const ScenarioRun replica
        = ScenarioRun::session(second, p_scenario, p_plant);
    run.witness(replica);
    return run;
}

TEST_CASE(
    "[Networked][Determinism][Declared][Law] an unimpaired lane reproduces"
) {
    const Scenario scenario = exact_lane();
    const ScenarioRun run = run_against_replica(scenario, PLANT_NONE);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REPRODUCIBLE, scenario);
    NETW_LAW_HOLDS(L_REPRODUCIBLE, run);
}

TEST_CASE(
    "[Networked][Determinism][Declared][Law] a seeded impairment reproduces"
) {
    const Scenario scenario = impaired_lane();
    const ScenarioRun run = run_against_replica(scenario, PLANT_NONE);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REPRODUCIBLE, scenario);
    NETW_LAW_HOLDS(L_REPRODUCIBLE, run);
}

TEST_CASE(
    "[Networked][Determinism][Declared][Law] an undeclared impulse breaks "
    "reproduction"
) {
    const Scenario scenario = impaired_lane();
    const ScenarioRun run
        = run_against_replica(scenario, PLANT_PHANTOM_PERTURB);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REPRODUCIBLE, scenario);
    NETW_LAW_BREAKS(L_REPRODUCIBLE, run);
}

} // namespace TestNetwDeclaredDeterminismLaws

#endif
