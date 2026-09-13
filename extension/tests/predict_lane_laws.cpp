// Laws for the prediction kernels COMPOSED: what a lane does over a whole run,
// rather than what one calculation answers for one transition.
//
// The corpus is the cross-product of the scenario table and the law table. One
// scenario is driven once and every law reads that run, so a new scenario or a
// new law costs one row and the cells it adds are free.
//
// Each law carries the plant that must break it. A law nothing can break is
// green for the same reason CHECK(true) is, and a matrix multiplies that: one
// wrong law silently weakens every cell it appears in.

#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

namespace TestNetwPredictLaneLaws {

using namespace netw_test;

Scenario clean_lane() {
    Scenario scenario;
    scenario.label = "clean-lane";
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

Scenario perturbed_lane() {
    Scenario scenario = clean_lane();
    scenario.label = "perturbed-lane";
    scenario.perturb(20, "P", godot::Vector2(6.0, 0.0));
    return scenario.until(60);
}

// Three disturbances on consecutive ticks, which outrun a convergence that
// closes half the gap per correction. This is the only lane here whose
// divergence does not shrink, and it is what an escalation is for.
Scenario drifting_lane() {
    Scenario scenario = clean_lane();
    scenario.label = "drifting-lane";
    scenario.perturb(20, "P", godot::Vector2(6.0, 0.0));
    scenario.perturb(21, "P", godot::Vector2(6.0, 0.0));
    scenario.perturb(22, "P", godot::Vector2(6.0, 0.0));
    return scenario.until(60);
}

Scenario stalled_input_lane() {
    Scenario scenario = clean_lane();
    scenario.label = "stalled-input-lane";
    scenario.release_input(25, "P");
    scenario.hold_input(30, "P", godot::Vector2(0.0, 1.0));
    return scenario.until(60);
}

int input_stimuli(const Scenario &p_scenario) {
    int count = 0;
    for (int index = 0; index < p_scenario.steps.size(); ++index) {
        const godot::StringName &verb = p_scenario.steps[index].verb;
        count += verb == godot::StringName("hold_input")
                || verb == godot::StringName("release_input")
            ? 1
            : 0;
    }
    return count;
}

LawVerdict law_drives_on_fresh_input_only(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    const int stimuli = input_stimuli(p_run.scenario());
    if (lane.consumed() != stimuli) {
        return law_broken(
            "drove %d fresh tick(s) for %d input stimuli",
            lane.consumed(),
            stimuli
        );
    }
    if (lane.consumed() + lane.missing() != p_run.scenario().run_ticks) {
        return law_broken(
            "%d driven + %d repeated tick(s) is not the %d the scenario runs",
            lane.consumed(),
            lane.missing(),
            p_run.scenario().run_ticks
        );
    }
    return law_held();
}

LawVerdict law_undisturbed_lanes_never_correct(const ScenarioRun &p_run) {
    if (p_run.scenario().declares("perturb")) {
        return law_held();
    }
    const Lane lane = p_run.lane("P");
    if (lane.corrections() != 0) {
        return law_broken(
            "an undisturbed lane took %d correction(s)",
            lane.corrections()
        );
    }
    if (lane.tail_divergence(p_run.scenario().run_ticks) > 0.0) {
        return law_broken(
            "an undisturbed lane diverged by %g",
            lane.tail_divergence(p_run.scenario().run_ticks)
        );
    }
    return law_held();
}

LawVerdict law_reconverges_after_its_last_stimulus(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    const double tail = lane.tail_divergence(5);
    if (tail >= lane.epsilon()) {
        return law_broken(
            "the last five ticks diverge by %g, at an epsilon of %g",
            tail,
            lane.epsilon()
        );
    }
    return law_held();
}

// Both directions, because a mechanism only proven green is a mechanism whose
// refusal nobody has seen. A converging recovery escalating is the defect a
// port of `escalation_after` is most likely to introduce.
LawVerdict law_only_a_non_shrinking_run_escalates(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    int disturbances = 0;
    const Scenario &scenario = p_run.scenario();
    for (int index = 0; index < scenario.steps.size(); ++index) {
        disturbances
            += scenario.steps[index].verb == godot::StringName("perturb") ? 1
                                                                          : 0;
    }
    if (disturbances >= 3) {
        if (lane.escalations() < 1) {
            return law_broken(
                "%d consecutive disturbances escalated %d time(s)",
                disturbances,
                lane.escalations()
            );
        }
        return law_held();
    }
    if (lane.escalations() != 0) {
        return law_broken(
            "a converging lane escalated %d time(s)",
            lane.escalations()
        );
    }
    return law_held();
}

const LawRow LAWS[] = {
    {"L-DRIVE",
     "a lane drives once per fresh input and repeats through every other "
     "tick",
     law_drives_on_fresh_input_only},
    {"L-QUIET",
     "a lane nothing disturbed neither diverges nor corrects",
     law_undisturbed_lanes_never_correct},
    {"L-CONV",
     "after its last stimulus a lane reconverges under epsilon",
     law_reconverges_after_its_last_stimulus},
    {"L-ESC",
     "escalation follows a non-shrinking run of divergence and nothing else",
     law_only_a_non_shrinking_run_escalates},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the lane laws hold across the corpus"
) {
    // Built here rather than at namespace scope: a `godot::String` cannot be
    // constructed before the library's entry point has run.
    const Scenario CORPUS[] = {
        clean_lane(),
        perturbed_lane(),
        drifting_lane(),
        stalled_input_lane(),
    };
    for (const Scenario &scenario : CORPUS) {
        const ScenarioRun run = ScenarioRun::kernel(scenario);
        REQUIRE(run.regime_reached());
        REQUIRE(run.decisions() > 0);
        for (const LawRow &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

// Each pair names the one scenario that carries the evidence the law reads. A
// plant against a scenario with nothing to see is a red proof of nothing:
// L-QUIET cannot be broken on a lane that was already perturbed, and L-CONV
// cannot be broken on a lane that never diverged.
struct RedProof {
    const LawRow &law;
    Scenario (*scenario)();
    Plant plant;
};

const RedProof RED_PROOFS[] = {
    {LAWS[0], clean_lane, PLANT_DRIVE_EVERY_TICK},
    {LAWS[1], clean_lane, PLANT_PHANTOM_PERTURB},
    {LAWS[2], perturbed_lane, PLANT_NO_RECOVER},
    {LAWS[3], perturbed_lane, PLANT_CARRY_STREAK},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every lane law breaks against its "
    "plant"
) {
    for (const RedProof &proof : RED_PROOFS) {
        const Scenario scenario = proof.scenario();
        const ScenarioRun planted = ScenarioRun::kernel(scenario, proof.plant);
        REQUIRE(planted.decisions() > 0);
        NETW_CELL(proof.law, scenario);
        NETW_LAW_BREAKS(proof.law, planted);
    }
}

} // namespace TestNetwPredictLaneLaws
