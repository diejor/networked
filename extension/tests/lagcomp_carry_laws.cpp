#include "support/netw_test.h"

#include "support/scenario_run.h"

#include "netw/api/prediction_handle.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwLagCompCarryLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::PackedInt32Array;
using godot::Ref;
using godot::StringName;
using godot::Vector2;

constexpr int SETTLE_FRAMES = 30;
constexpr int RECOVER_FRAMES = 70;
constexpr double TELEPORT_AT = 200.0;

EntityDecl carrying_player(double p_gain) {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::FRAME)
        .missing_input(netw::MissingInput::STALL)
        .carried(p_gain, TELEPORT_AT);
}

Scenario carrying_lane(double p_gain) {
    Scenario scenario;
    scenario.label = "carrying-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        carrying_player(p_gain),
        0
    );
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    scenario.perturb(SETTLE_FRAMES + 1, "P", Vector2(60.0, -40.0));
    PackedInt32Array ticks;
    for (int frame = 0; frame < SETTLE_FRAMES + RECOVER_FRAMES; ++frame) {
        ticks.push_back(1);
    }
    scenario.schedule(ticks);
    return scenario.until(int(ticks.size()));
}

constexpr int LEDGER_CARRIED = 3;
constexpr int LEDGER_INFIDELITY = 5;

godot::PackedInt64Array position_ledger(LoopbackRig &p_rig) {
    netw::NetwPredictionEngine *const pool = p_rig.prediction_pool(0);
    const int64_t slot = p_rig.prediction_slot(StringName("P"), 0);
    REQUIRE_MESSAGE(
        pool->ledger_fields(slot).has(StringName("position")),
        "the position field has no ledger row"
    );
    return pool->ledger_counts(slot, StringName("position"));
}

TEST_CASE(
    "[Networked][LagComp][Carry] CR1 a rule that reproduces the "
    "transitions it is replayed against advances the acknowledged write to "
    "the present, so a declared forward model answers a correction instead "
    "of being declined"
) {
    const Scenario scenario = carrying_lane(1.0);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const godot::PackedInt64Array ledger = position_ledger(rig);
    REQUIRE(ledger.size() > LEDGER_INFIDELITY);

    NETW_CHECK_GT(ledger[LEDGER_CARRIED], int64_t(0));
    NETW_CHECK_EQ(ledger[LEDGER_INFIDELITY], int64_t(0));
}

TEST_CASE(
    "[Networked][LagComp][Carry] CR2 a rule that overstates every "
    "transition disagrees with the past it is replayed against, so the "
    "fidelity gate catches it and nothing it computed reaches the body"
) {
    const Scenario scenario = carrying_lane(2.0);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const godot::PackedInt64Array ledger = position_ledger(rig);
    REQUIRE(ledger.size() > LEDGER_INFIDELITY);

    NETW_CHECK_GT(ledger[LEDGER_INFIDELITY], int64_t(0));
    NETW_CHECK_EQ(ledger[LEDGER_CARRIED], int64_t(0));
}

} // namespace TestNetwLagCompCarryLaws

#endif
