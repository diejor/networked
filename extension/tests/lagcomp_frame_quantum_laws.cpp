#include "support/netw_test.h"

#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"

namespace TestNetwLagCompFrameQuantumLaws {

using namespace netw_test;
using godot::PackedInt32Array;
using godot::PackedInt64Array;
using godot::Ref;
using godot::StringName;
using godot::Vector2;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;

constexpr int FRAMES = 40;
constexpr int PHYSICS_FACTOR = 2;

EntityDecl framed_player() {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::FRAME)
        .missing_input(netw::MissingInput::STALL);
}

Scenario framed_lane(const char *p_label, bool p_hold_every_other) {
    Scenario scenario;
    scenario.label = p_label;
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(framed_player(), 0);
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    PackedInt32Array ticks;
    for (int frame = 0; frame < FRAMES; ++frame) {
        ticks.push_back(
            p_hold_every_other && frame % PHYSICS_FACTOR == 1 ? 0 : 1
        );
    }
    scenario.schedule(ticks);
    return scenario.until(int(ticks.size()));
}

PackedInt64Array pool_book(LoopbackRig &p_rig) {
    NetwMultiplayer *core = p_rig.client(0);
    REQUIRE_MESSAGE(core != nullptr, "the client has no native core");
    if (core == nullptr) {
        return PackedInt64Array();
    }
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const Ref<godot::RefCounted> seated
        = core->entity_get_view(p_rig.entity_of(StringName("P"), 0));
    REQUIRE(seated.is_valid());
    const int64_t slot = pool->slot_of(seated);
    REQUIRE(slot >= 0);
    return pool->drive_stats(slot);
}

TEST_CASE(
    "[Networked][LagComp][Quantum] FQ1 a frame-tier transition is a "
    "quantum of physics frames the clock itself declares, and a frame the "
    "clock held bought no simulated time, so it opens no transition and the "
    "quantum stays a count of frames rather than of drives"
) {
    const Scenario scenario = framed_lane("quantum-lane", true);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const Lane lane = run.lane(StringName("P"));
    NETW_CHECK_EQ(lane.quantum_declared(), PHYSICS_FACTOR);
    NETW_CHECK_EQ(lane.quantum_steps(), lane.quantum_declared());
    NETW_CHECK_EQ(lane.quantum_faults(), 0);
    NETW_CHECK_GT(lane.authoring_clamped(), 0);
}

TEST_CASE(
    "[Networked][LagComp][Quantum] FQ2 a pair driven off the cadence "
    "its clock declares is charged a fault on BOTH books, which the pool can "
    "only keep because it is told the declaration rather than handed the "
    "measurement it would always agree with"
) {
    const Scenario scenario = framed_lane("off-quantum-lane", false);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const Lane lane = run.lane(StringName("P"));
    NETW_CHECK_EQ(lane.quantum_declared(), PHYSICS_FACTOR);
    NETW_CHECK_LT(lane.quantum_steps(), lane.quantum_declared());
    NETW_CHECK_GT(lane.quantum_faults(), 0);

    const PackedInt64Array book = pool_book(rig);
    REQUIRE(book.size() > NetwPredictionEngine::STAT_QUANTUM_FAULTS);
    NETW_CHECK_EQ(
        book[NetwPredictionEngine::STAT_QUANTUM_DECLARED],
        PHYSICS_FACTOR
    );
    NETW_CHECK_GT(book[NetwPredictionEngine::STAT_QUANTUM_FAULTS], 0);
}

} // namespace TestNetwLagCompFrameQuantumLaws

#endif
