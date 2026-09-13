#include "support/netw_test.h"

#include "support/scenario_run.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/timeline.hpp"
#include "netw/lagcomp_core.hpp"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwLagCompInputHistoryLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::Ref;
using godot::StringName;
using godot::Vector2;
using netw::NetwEntity;
using netw::NetwLagCompCore;
using netw::NetwMultiplayer;
using netw::NetwTimeline;

constexpr double TOLERANCE = 0.001;

EntityDecl consumed_player() {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario consumed_lane() {
    Scenario scenario;
    scenario.label = "consumed-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        consumed_player(),
        0
    );
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    return scenario.until(80);
}

double x_of(const Dictionary &p_row) {
    return double(Vector2(p_row[StringName("position")]).x);
}

TEST_CASE(
    "[Networked][LagComp][History] IH1 authority keys the state a "
    "consumed input produced by that INPUT tick, so a rewind to the tick the "
    "owner predicted reads the owner's own answer rather than a server-clock "
    "slot the input had not reached yet"
) {
    const Scenario scenario = consumed_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    netw::NetwPredictionHandle *owner_handle
        = rig.prediction_handle(StringName("P"), 0);
    REQUIRE(owner_handle != nullptr);
    const int64_t ack = owner_handle->get_acknowledged_tick();
    NETW_CHECK_GT(ack, int64_t(0));

    const int64_t state_tick = ack + 1;

    const Ref<NetwEntity> owned
        = NetwEntity::of(rig.node_of(rig.entity_of(StringName("P"), 0), 0));
    REQUIRE(owned.is_valid());
    const Ref<NetwTimeline> owner_history = owned->get_timeline();
    REQUIRE(owner_history.is_valid());
    const Dictionary predicted
        = owner_history->latest_state_at_or_before(state_tick);
    REQUIRE_MESSAGE(
        !predicted.is_empty(),
        "the owner kept no prediction at the tick it was acked for"
    );

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    NetwLagCompCore *history = server->get_lagcomp_core();
    REQUIRE(history != nullptr);
    const Ref<godot::RefCounted> seated
        = server->entity_get_view(rig.entity_of(StringName("P")));
    REQUIRE(seated.is_valid());

    const Dictionary sampled
        = history->timeline_sample_entity(seated, state_tick);
    REQUIRE_MESSAGE(
        !sampled.is_empty(),
        "authority recorded nothing at the tick it acknowledged"
    );

    NETW_CHECK_CLOSE(x_of(sampled), x_of(predicted), TOLERANCE);
}

} // namespace TestNetwLagCompInputHistoryLaws

#endif
