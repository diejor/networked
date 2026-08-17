#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwDeclaredRecordLaws {

using namespace netw_test;

// Far enough back that the body has visibly moved since, and inside the
// retained window at any tickrate this file declares.
constexpr int PAST_TICK = 8;

// A body nobody predicts, declaring state and nothing else. Its timeline is
// owed to that declaration alone, which is the whole claim.
EntityDecl recorded_platform() {
    return EntityDecl()
        .named("Platform")
        .on_schema("PlatformPose")
        .synced("position")
        .placed_at(godot::Vector2());
}

Scenario recorded_lane() {
    Scenario scenario;
    scenario.label = "recorded-lane";
    scenario.world.clocked(30, 3).lag_compensated().entity(
        recorded_platform()
    );
    scenario.hold_input(1, "Platform", godot::Vector2(5.0, 0.0));
    scenario.sample_at(PAST_TICK, "Platform");
    return scenario.until(24);
}

Scenario rewound_lane() {
    Scenario scenario = recorded_lane();
    scenario.label = "rewound-lane";
    scenario.rewind_at(PAST_TICK, "Platform");
    return scenario;
}

// Before the run began, so nothing was ever retained there. A rewind to it is
// a question the history cannot answer rather than one it answers wrongly.
constexpr int UNRETAINED_TICK = -100;

Scenario unretained_rewind() {
    Scenario scenario = recorded_lane();
    scenario.label = "unretained-rewind";
    scenario.rewind_at(UNRETAINED_TICK, "Platform");
    return scenario;
}

// An entity a client controls, so BOTH peers hold a handle for it and the same
// question can be put to each. The server-only platform above is mirrored
// nowhere, which makes it useless for a law about what a peer answers.
Scenario mirrored_lane() {
    Scenario scenario;
    scenario.label = "mirrored-lane";
    scenario.world.clocked(30, 3).lag_compensated().player(
        EntityDecl()
            .named("Platform")
            .on_schema("PlatformPose")
            .synced("position")
            .placed_at(godot::Vector2())
            .predicted(),
        0
    );
    scenario.hold_input(1, "Platform", godot::Vector2(5.0, 0.0));
    scenario.sample_at(PAST_TICK, "Platform");
    return scenario.until(24);
}

// A view tick older than anything the run retained. A compensator asking one
// is asking a question the history cannot answer, and the answer it is owed is
// "nothing" rather than the oldest thing retained.
Scenario unretained_sample() {
    Scenario scenario = recorded_lane();
    scenario.label = "unretained-sample";
    scenario.sample_at(UNRETAINED_TICK, "Platform");
    return scenario;
}

Scenario retired_lane() {
    Scenario scenario = recorded_lane();
    scenario.label = "retired-lane";
    scenario.undeclare(12, "Platform");
    return scenario;
}

// What LagCompensationMonitor reads out of the session. A key it cannot
// find reads as a rate of zero, which is why presence is the law rather than
// the value.
const char *const MONITOR_KEYS[] = {
    "entities",
    "timelines",
    "corrections",
    "max_replay_depth",
    "consumed",
    "missing",
    "pending_actions",
    "effects_armed",
    "gate_fallbacks",
};

// Timelines the scenario is owed at the end of its run: one per entity that
// declares state, less every one it retired along the way.
int declared_timelines(const Scenario &p_scenario) {
    int owed = 0;
    for (int index = 0; index < p_scenario.world.entity_count(); ++index) {
        const EntityDecl &decl = p_scenario.world.entity_at(index);
        if (decl.schema().is_empty()) {
            continue;
        }
        bool retired = false;
        for (const Scenario::Step &step : p_scenario.steps) {
            retired = retired
                || (step.verb == godot::StringName("undeclare")
                    && step.subject == decl.name());
        }
        owed += retired ? 0 : 1;
    }
    return owed;
}

LawVerdict law_occupancy_counts_what_it_records(const ScenarioRun &p_run) {
    const Occupancy &occupancy = p_run.occupancy();
    if (!occupancy.taken()) {
        return law_broken("the session reported no occupancy");
    }
    for (const char *const key : MONITOR_KEYS) {
        if (!occupancy.reports(godot::StringName(key))) {
            return law_broken("the session does not report '%s'", key);
        }
    }
    const int owed = declared_timelines(p_run.scenario());
    if (occupancy.timelines() != owed) {
        return law_broken(
            "the session holds %d timeline(s) where %d were declared",
            occupancy.timelines(),
            owed
        );
    }
    return law_held();
}

LawVerdict law_history_holds_the_past(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (!lane.sample_found()) {
        return law_broken("the lane's history answered nothing");
    }
    if (lane.sample_x() >= lane.authority_position_x()) {
        return law_broken(
            "history reads %g where the body now stands at %g",
            lane.sample_x(),
            lane.authority_position_x()
        );
    }
    if (lane.sample_x() <= 0.0) {
        return law_broken(
            "history reads %g, which is where the body started",
            lane.sample_x()
        );
    }
    return law_held();
}

LawVerdict law_retired_history_answers_nothing(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (lane.authority_position_x() <= 0.0) {
        return law_broken("the body never moved, so nothing was retired");
    }
    if (lane.sample_found()) {
        return law_broken(
            "a retired timeline still reads %g",
            lane.sample_x()
        );
    }
    return law_held();
}

LawVerdict law_rewind_shows_the_past_and_restores(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (lane.rewind_visits() != 1) {
        return law_broken(
            "the rewind ran its body %d time(s)",
            lane.rewind_visits()
        );
    }
    if (!lane.sample_found()) {
        return law_broken("the lane had no past to be rewound to");
    }
    if (godot::Math::abs(lane.rewound_x() - lane.sample_x()) > 0.001) {
        return law_broken(
            "the body stood at %g inside a rewind to %g",
            lane.rewound_x(),
            lane.sample_x()
        );
    }
    if (godot::Math::abs(lane.restored_x() - lane.position_x()) > 0.001) {
        return law_broken(
            "the body returned to %g from a run that left it at %g",
            lane.restored_x(),
            lane.position_x()
        );
    }
    return law_held();
}

LawVerdict law_unretained_rewind_moves_nothing(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (lane.rewind_visits() != 1) {
        return law_broken(
            "the rewind ran its body %d time(s)",
            lane.rewind_visits()
        );
    }
    if (godot::Math::abs(lane.rewound_x() - lane.position_x()) > 0.001) {
        return law_broken(
            "the body stood at %g inside a rewind to a past nothing retained, "
            "where it lives at %g",
            lane.rewound_x(),
            lane.position_x()
        );
    }
    if (godot::Math::abs(lane.restored_x() - lane.position_x()) > 0.001) {
        return law_broken(
            "the body returned to %g from a run that left it at %g",
            lane.restored_x(),
            lane.position_x()
        );
    }
    return law_held();
}

LawVerdict law_unretained_sample_answers_nothing(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (lane.authority_position_x() <= 0.0) {
        return law_broken("the body never moved, so nothing was retained");
    }
    if (lane.sample_found()) {
        return law_broken(
            "a tick nothing retained answered %g",
            lane.sample_x()
        );
    }
    return law_held();
}

LawVerdict law_only_authority_answers_history(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("Platform");
    if (!lane.sample_found()) {
        return law_broken("authority answered nothing, so nothing was asked");
    }
    if (!lane.peer_asked()) {
        return law_broken("no peer held the entity, so none was asked");
    }
    if (lane.peer_sample_found()) {
        return law_broken(
            "a peer that records no authoritative history answered a sample"
        );
    }
    return law_held();
}

const LawRow L_OFF_SERVER = {
    "L-OFF-SERVER",
    "only the peer that records authoritative history answers a sample, and "
    "one that does not degrades to nothing rather than fabricating",
    law_only_authority_answers_history,
};

const LawRow L_CLAMP = {
    "L-CLAMP",
    "a sample of a tick older than anything retained answers nothing rather "
    "than the oldest thing it holds",
    law_unretained_sample_answers_nothing,
};

const LawRow L_UNRETAINED = {
    "L-UNRETAINED",
    "a rewind to a tick nothing was retained at still runs its body and "
    "leaves it where it lives",
    law_unretained_rewind_moves_nothing,
};

const LawRow L_REWIND = {
    "L-REWIND",
    "a rewind stands the live body where its history was and returns it "
    "where the run left it",
    law_rewind_shows_the_past_and_restores,
};

const LawRow L_RECORD = {
    "L-RECORD",
    "an entity that declares state alone is recorded, and its history reads "
    "where it was rather than where it is",
    law_history_holds_the_past,
};

const LawRow L_OCCUPANCY = {
    "L-OCCUPANCY",
    "the session reports every occupancy key its monitor reads and holds one "
    "timeline per entity still declaring state",
    law_occupancy_counts_what_it_records,
};

const LawRow L_RETIRED = {
    "L-RETIRED",
    "an undeclared timeline answers nothing",
    law_retired_history_answers_nothing,
};

TEST_CASE(
    "[Networked][Record][Declared][Law] a state-only entity keeps its past"
) {
    const Scenario scenario = recorded_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RECORD, scenario);
    NETW_LAW_HOLDS(L_RECORD, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] an unrecorded entity keeps no past"
) {
    const Scenario scenario = recorded_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_NO_HISTORY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RECORD, scenario);
    NETW_LAW_BREAKS(L_RECORD, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a rewind shows the past and restores"
) {
    const Scenario scenario = rewound_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REWIND, scenario);
    NETW_LAW_HOLDS(L_REWIND, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a rewind with no history moves "
    "nothing"
) {
    const Scenario scenario = rewound_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_NO_HISTORY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REWIND, scenario);
    NETW_LAW_BREAKS(L_REWIND, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a peer that records no history "
    "answers no sample"
) {
    const Scenario scenario = mirrored_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OFF_SERVER, scenario);
    NETW_LAW_HOLDS(L_OFF_SERVER, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a peer answering out of authority's "
    "history breaks the sample"
) {
    const Scenario scenario = mirrored_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_PEER_READS_AUTHORITY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OFF_SERVER, scenario);
    NETW_LAW_BREAKS(L_OFF_SERVER, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a view tick older than the retained "
    "window answers nothing"
) {
    const Scenario scenario = unretained_sample();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_CLAMP, scenario);
    NETW_LAW_HOLDS(L_CLAMP, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] clamping an unanswerable view tick "
    "into the window breaks the sample"
) {
    const Scenario scenario = unretained_sample();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_RETAINED_SAMPLE);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_CLAMP, scenario);
    NETW_LAW_BREAKS(L_CLAMP, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a retired timeline stops answering"
) {
    const Scenario scenario = retired_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RETIRED, scenario);
    NETW_LAW_HOLDS(L_RETIRED, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a timeline kept past its retirement "
    "still answers"
) {
    const Scenario scenario = retired_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_KEEP_HISTORY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RETIRED, scenario);
    NETW_LAW_BREAKS(L_RETIRED, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] a rewind to an unretained past leaves "
    "the body alone"
) {
    const Scenario scenario = unretained_rewind();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_UNRETAINED, scenario);
    NETW_LAW_HOLDS(L_UNRETAINED, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] an unretained past served from a "
    "retained one moves the body"
) {
    const Scenario scenario = unretained_rewind();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_RETAINED_REWIND);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_UNRETAINED, scenario);
    NETW_LAW_BREAKS(L_UNRETAINED, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] the session holds the timeline it "
    "declared"
) {
    const Scenario scenario = recorded_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OCCUPANCY, scenario);
    NETW_LAW_HOLDS(L_OCCUPANCY, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] the session holds no timeline nobody "
    "kept"
) {
    const Scenario scenario = recorded_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_NO_HISTORY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OCCUPANCY, scenario);
    NETW_LAW_BREAKS(L_OCCUPANCY, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] the session gives back a retired "
    "timeline"
) {
    const Scenario scenario = retired_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::record(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OCCUPANCY, scenario);
    NETW_LAW_HOLDS(L_OCCUPANCY, run);
}

TEST_CASE(
    "[Networked][Record][Declared][Law] the session still holds a timeline "
    "kept past its retirement"
) {
    const Scenario scenario = retired_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::record(rig, scenario, PLANT_KEEP_HISTORY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_OCCUPANCY, scenario);
    NETW_LAW_BREAKS(L_OCCUPANCY, run);
}

} // namespace TestNetwDeclaredRecordLaws

#endif
