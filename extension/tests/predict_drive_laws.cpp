// Laws for the drive path an engine OWNS: the tape it authors, the journal row
// each pass opens and closes, and the horizon that stops it speculating past
// what authority has confirmed.
//
// These are the questions `predict_lane_laws.cpp` cannot ask. A kernel is a
// function and has no memory, so a law about what an engine remembers between
// two ticks has to stand on a driver that holds an engine.

#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

namespace TestNetwPredictDriveLaws {

using namespace netw_test;

Scenario driven_lane() {
    Scenario scenario;
    scenario.label = "driven-lane";
    scenario.epsilon = 0.5;
    scenario.warmup_ticks = 4;
    scenario.world.entity(
        EntityDecl()
            .named("P")
            .synced("position")
            .placed_at(godot::Vector2(0.0, 0.0))
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(40);
}

// Long enough that the speculative span reaches its bound, which no scenario
// shorter than the bound can show anything about.
Scenario unacknowledged_lane() {
    Scenario scenario = driven_lane();
    scenario.label = "unacknowledged-lane";
    return scenario.until(netw::predict::ACK_AGE_MAX * 2);
}

Scenario corrected_lane() {
    Scenario scenario = driven_lane();
    scenario.label = "corrected-lane";
    scenario.perturb(20, "P", godot::Vector2(6.0, 0.0));
    return scenario.until(60);
}

LawVerdict law_every_opened_row_closes(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.journal_rows() != lane.closed_rows()) {
        return law_broken(
            "%d of %d journal row(s) never closed",
            lane.journal_rows() - lane.closed_rows(),
            lane.journal_rows()
        );
    }
    if (lane.journal_rows() != lane.consumed()) {
        return law_broken(
            "%d journal row(s) for %d drive(s) that ran",
            lane.journal_rows(),
            lane.consumed()
        );
    }
    return law_held();
}

LawVerdict law_an_uncorrected_lane_chains(const ScenarioRun &p_run) {
    // A correction writes the body from outside the engine, and a write no
    // operator accounts for IS a chain break. Only a lane nothing corrected
    // says anything about whether the drive path chains on its own.
    if (p_run.scenario().declares("perturb")) {
        return law_held();
    }
    const Lane lane = p_run.lane("P");
    if (lane.chain_breaks() != 0) {
        return law_broken(
            "an uncorrected lane broke its state chain %d time(s)",
            lane.chain_breaks()
        );
    }
    return law_held();
}

LawVerdict law_speculation_stops_at_the_horizon(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.journal_rows() > netw::predict::ACK_AGE_MAX) {
        return law_broken(
            "a lane nothing acknowledged opened %d transition(s) past a "
            "horizon of %d",
            lane.journal_rows(),
            netw::predict::ACK_AGE_MAX
        );
    }
    if (p_run.scenario().run_ticks <= netw::predict::ACK_AGE_MAX) {
        return law_held();
    }
    if (lane.held() == 0) {
        return law_broken(
            "a lane ran %d tick(s) past a horizon of %d without holding once",
            p_run.scenario().run_ticks,
            netw::predict::ACK_AGE_MAX
        );
    }
    return law_held();
}

const LawRow LAWS[] = {
    { "L-CLOSE",
      "every journal row a drive opened is closed by that drive, and there is "
      "one row per drive that ran",
      law_every_opened_row_closes },
    { "L-CHAIN",
      "a lane nothing corrected enters each transition holding the state the "
      "previous one produced",
      law_an_uncorrected_lane_chains },
    { "L-HORIZON",
      "a lane authority never acknowledges stops opening transitions at the "
      "speculative bound",
      law_speculation_stops_at_the_horizon },
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the drive laws hold across the corpus"
) {
    const Scenario CORPUS[] = {
        driven_lane(),
        unacknowledged_lane(),
        corrected_lane(),
    };
    for (const Scenario &scenario : CORPUS) {
        const ScenarioRun run = ScenarioRun::lanes(scenario);
        REQUIRE(run.regime_reached());
        REQUIRE(run.decisions() > 0);
        for (const LawRow &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

struct RedProof {
    const LawRow &law;
    Scenario (*scenario)();
    Plant plant;
};

// Each pair names the one scenario carrying the evidence the law reads.
// L-HORIZON cannot be broken on a lane shorter than the bound, and L-CHAIN
// cannot be broken on a lane a correction already broke.
const RedProof RED_PROOFS[] = {
    { LAWS[0], driven_lane, PLANT_SKIP_CLOSE },
    { LAWS[1], driven_lane, PLANT_FORGE_PRE },
    { LAWS[2], unacknowledged_lane, PLANT_ALWAYS_ACK },
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every drive law breaks against its "
    "plant"
) {
    for (const RedProof &proof : RED_PROOFS) {
        const Scenario scenario = proof.scenario();
        const ScenarioRun planted = ScenarioRun::lanes(scenario, proof.plant);
        REQUIRE(planted.decisions() > 0);
        NETW_CELL(proof.law, scenario);
        NETW_LAW_BREAKS(proof.law, planted);
    }
}

Scenario missing_input_lane(netw::MissingInput p_policy) {
    Scenario scenario;
    scenario.label = p_policy == netw::MissingInput::STALL
        ? "stalled-input-lane"
        : "repeat-last-input-lane";
    scenario.world.entity(
        EntityDecl()
            .named("P")
            .synced("position")
            .placed_at(godot::Vector2())
            .missing_input(p_policy)
    );
    scenario.input_at(1, "P", godot::Vector2(1.0, 0.0));
    scenario.input_at(3, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(3);
}

LawVerdict law_missing_input_policy(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.missing() != 1 || lane.consumed() != 2) {
        return law_broken(
            "the lane consumed %d fresh input(s) and found %d missing",
            lane.consumed(),
            lane.missing()
        );
    }
    const netw::MissingInput policy
        = p_run.scenario().world.entity_at(0).missing_input_policy();
    const double expected = policy == netw::MissingInput::STALL ? 2.0 : 3.0;
    const double actual = godot::Vector2(lane.position()).x;
    if (std::abs(actual - expected) > 0.001) {
        return law_broken(
            "the policy advanced the lane by %g rather than %g",
            actual,
            expected
        );
    }
    return law_held();
}

const LawRow L_MISSING = {
    "L-MISSING",
    "a missing input advances once under REPEAT_LAST and never under STALL",
    law_missing_input_policy,
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] missing input obeys the declared policy"
) {
    const netw::MissingInput POLICIES[] = {
        netw::MissingInput::STALL,
        netw::MissingInput::REPEAT_LAST,
    };
    for (netw::MissingInput policy : POLICIES) {
        const Scenario scenario = missing_input_lane(policy);
        const ScenarioRun run = ScenarioRun::consume(scenario);
        REQUIRE(run.regime_reached());
        REQUIRE(run.decisions() == 3);
        NETW_CELL(L_MISSING, scenario);
        NETW_LAW_HOLDS(L_MISSING, run);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] STALL breaks when a missing input "
    "repeats"
) {
    const Scenario scenario = missing_input_lane(netw::MissingInput::STALL);
    const ScenarioRun run
        = ScenarioRun::consume(scenario, PLANT_REPEAT_MISSING);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MISSING, scenario);
    NETW_LAW_BREAKS(L_MISSING, run);
}

} // namespace TestNetwPredictDriveLaws
