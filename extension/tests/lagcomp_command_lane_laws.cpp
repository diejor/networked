#include "support/netw_test.h"

#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/templates.hpp"
#include "netw/api/prediction_handle.hpp"

namespace TestNetwLagCompCommandLaneLaws {

using namespace netw_test;
using godot::Array;
using godot::Dictionary;
using godot::HashMap;
using godot::PackedInt32Array;
using godot::StringName;
using godot::Vector2;

constexpr int FRAMES = 40;

EntityDecl authoring_player() {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::FRAME)
        .missing_input(netw::MissingInput::STALL);
}

Scenario command_lane() {
    Scenario scenario;
    scenario.label = "command-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        authoring_player(),
        0
    );
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    PackedInt32Array ticks;
    for (int frame = 0; frame < FRAMES; ++frame) {
        ticks.push_back(frame % 5 == 3 ? 0 : 1);
    }
    scenario.schedule(ticks);
    return scenario.until(int(ticks.size()));
}

HashMap<int64_t, Dictionary> tape_of(netw::NetwPredictionHandle *p_handle) {
    HashMap<int64_t, Dictionary> out;
    REQUIRE_MESSAGE(p_handle != nullptr, "a tape needs a prediction handle");
    if (p_handle == nullptr) {
        return out;
    }
    const Array rows = p_handle->tape_transitions();
    for (int at = 0; at < rows.size(); ++at) {
        const Dictionary entry = rows[at];
        out[int64_t(entry[StringName("index")])] = entry;
    }
    return out;
}

TEST_CASE(
    "[Networked][LagComp][Lane] CL1 the transitions a predicting "
    "owner authored are the transitions its authority replays, under the "
    "labels and the freshness the owner gave them, because the lane re-sends "
    "an overlapping window and a consumer that lost freshness would count a "
    "held frame as a fresh command"
) {
    const Scenario scenario = command_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const HashMap<int64_t, Dictionary> authored
        = tape_of(rig.prediction_handle(StringName("P"), 0));
    const HashMap<int64_t, Dictionary> decoded
        = tape_of(rig.prediction_handle(StringName("P")));

    REQUIRE_MESSAGE(
        decoded.size() > 0,
        "a consuming peer that decoded nothing proves nothing about the lane"
    );

    int compared = 0;
    for (const godot::KeyValue<int64_t, Dictionary> &row : decoded) {
        const Dictionary *owner = authored.getptr(row.key);
        if (owner == nullptr) {
            continue;
        }
        const Dictionary &replayed = row.value;
        NETW_CHECK_EQ(
            int64_t(replayed[StringName("label")]),
            int64_t((*owner)[StringName("label")])
        );
        NETW_CHECK_EQ(
            int(bool(replayed[StringName("fresh")])),
            int(bool((*owner)[StringName("fresh")]))
        );
        compared += 1;
    }

    REQUIRE_MESSAGE(
        compared > 0,
        "the two tapes held no transition in common to disagree about"
    );
}

} // namespace TestNetwLagCompCommandLaneLaws

#endif
