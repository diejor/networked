#include "support/netw_test.h"

#include "support/netw_call_log.h"
#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"

namespace TestNetwLagCompEpisodeReportLaws {

using namespace netw_test;
using godot::Array;
using godot::Dictionary;
using godot::PackedInt64Array;
using godot::Ref;
using godot::StringName;
using godot::Vector;
using godot::Vector2;
using netw::EventPlane;
using netw::NetwMultiplayer;
using netw::NetwPredict;

const Vector2 OFFSET = Vector2(60.0, -40.0);

EntityDecl watched_player() {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario watched_lane(bool p_perturbed) {
    Scenario scenario;
    scenario.label = p_perturbed ? "episode-lane" : "stage-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(watched_player(), 0);
    if (p_perturbed) {
        scenario.conditions(netw::LocalLinkConditions::polls(4));
    }
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    if (!p_perturbed) {
        return scenario.until(20);
    }
    scenario.perturb(31, "P", OFFSET);
    return scenario.until(100);
}

void watch(
    netw::NetwMultiplayer *p_api,
    const PackedInt64Array &p_events,
    const CallLog &p_log,
    const char *p_tag
) {
    REQUIRE_MESSAGE(p_api != nullptr, "a watch needs a native core");
    if (p_api == nullptr) {
        return;
    }
    p_api->event_watch(
        p_events,
        Dictionary(),
        Dictionary(),
        p_log.callable(StringName(p_tag)),
        Dictionary()
    );
}

PackedInt64Array episode_events() {
    PackedInt64Array events;
    events.push_back(EventPlane::EPISODE_OPEN);
    events.push_back(EventPlane::EPISODE_CLOSE);
    events.push_back(EventPlane::EPISODE_FALLBACK);
    events.push_back(EventPlane::DIVERGENCE);
    events.push_back(EventPlane::RECOVERY);
    return events;
}

PackedInt64Array stage_events() {
    PackedInt64Array events;
    events.push_back(EventPlane::PREDICT_DRIVE);
    events.push_back(EventPlane::PREDICT_CONSUME);
    events.push_back(EventPlane::PREDICT_EVALUATE);
    events.push_back(EventPlane::PREDICT_RECOVER);
    return events;
}

int64_t row_event(const Dictionary &p_row) {
    return int64_t(p_row[netw::event_key::event()]);
}

Dictionary row_detail(const Dictionary &p_row) {
    return p_row[netw::event_key::detail()];
}

Dictionary row_model(const Dictionary &p_row) {
    return p_row[netw::event_key::model()];
}

Vector<Dictionary> reported(const CallLog &p_log, const char *p_tag) {
    Vector<Dictionary> out;
    const int seen = p_log.count(StringName(p_tag));
    for (int at = 0; at < seen; ++at) {
        const Array said = p_log.args(StringName(p_tag), at);
        REQUIRE_MESSAGE(said.size() == 1, "an event row carries one event");
        if (said.size() == 1) {
            out.push_back(Dictionary(said[0]));
        }
    }
    return out;
}

Vector<Dictionary> rows_of(const Vector<Dictionary> &p_rows, int64_t p_event) {
    Vector<Dictionary> kept;
    for (int at = 0; at < p_rows.size(); ++at) {
        if (row_event(p_rows[at]) == p_event) {
            kept.push_back(p_rows[at]);
        }
    }
    return kept;
}

int64_t detail_int(const Dictionary &p_row, const char *p_key) {
    return int64_t(row_detail(p_row).get(StringName(p_key), -1));
}

TEST_CASE(
    "[Networked][LagComp][Episode] EP1 a watched owner is told the "
    "disagreement BEFORE the episode opened to supervise it, then the write "
    "that answered it, and finally the close, because a divergence is a "
    "comparison's own verdict while an episode is what the pool does about it"
) {
    const Scenario scenario = watched_lane(true);
    LoopbackRig rig(scenario.clients);
    const CallLog log;
    watch(rig.client(0), episode_events(), log, "owner");

    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const Vector<Dictionary> rows = reported(log, "owner");
    REQUIRE(rows.size() >= 4);

    NETW_CHECK_EQ(row_event(rows[0]), int64_t(EventPlane::DIVERGENCE));
    NETW_CHECK_EQ(row_event(rows[1]), int64_t(EventPlane::EPISODE_OPEN));
    NETW_CHECK_EQ(row_event(rows[2]), int64_t(EventPlane::RECOVERY));
    NETW_CHECK_EQ(
        row_event(rows[rows.size() - 1]),
        int64_t(EventPlane::EPISODE_CLOSE)
    );

    const int64_t transition = detail_int(rows[0], "transition");
    NETW_CHECK_GE(transition, int64_t(0));
    NETW_CHECK_GT(
        double(row_detail(rows[0]).get(StringName("divergence"), 0.0)),
        0.0
    );
    NETW_CHECK_EQ(detail_int(rows[1], "transition"), transition);
    CHECK(row_model(rows[1]).has(StringName("generator")));

    const Dictionary written = rows[2];
    NETW_CHECK_EQ(detail_int(written, "transition"), transition);
    CHECK_FALSE(bool(row_detail(written).get(StringName("teleport"), true)));
    const Dictionary moved
        = row_detail(written).get(StringName("moved"), Dictionary());
    REQUIRE(moved.has(StringName("position")));
    NETW_CHECK_LT(
        double(Vector2(moved[StringName("position")]).distance_to(OFFSET)),
        0.01
    );

    SUBCASE(
        "and an acknowledgement already in flight when the write landed still "
        "disagrees, so it names a LATER transition and earns no second write"
    ) {
        for (int at = 3; at < rows.size() - 1; ++at) {
            NETW_CHECK_EQ(row_event(rows[at]), int64_t(EventPlane::DIVERGENCE));
            NETW_CHECK_GT(detail_int(rows[at], "transition"), transition);
        }
    }

    const Dictionary closed = rows[rows.size() - 1];
    NETW_CHECK_EQ(detail_int(closed, "transition"), transition);
    NETW_CHECK_EQ(
        detail_int(closed, "state"),
        int64_t(NetwPredict::EPISODE_STATE_CLOSED)
    );
    NETW_CHECK_GT(detail_int(closed, "closed_transition"), transition);
}

TEST_CASE(
    "[Networked][LagComp][Episode] EP2 every driven pass files the "
    "transition it opened and whether it drove fresh, and consuming is the "
    "other side of that pass, which only authority runs, so an owner never "
    "files one"
) {
    const Scenario scenario = watched_lane(false);
    LoopbackRig rig(scenario.clients);
    const CallLog log;
    watch(rig.client(0), stage_events(), log, "owner");
    watch(rig.server(), stage_events(), log, "authority");

    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const Vector<Dictionary> owner = reported(log, "owner");
    const Vector<Dictionary> authority = reported(log, "authority");

    const Vector<Dictionary> drives = rows_of(owner, EventPlane::PREDICT_DRIVE);
    REQUIRE(drives.size() > 0);
    CHECK(bool(row_detail(drives[0]).get(StringName("fresh"), false)));
    NETW_CHECK_GE(detail_int(drives[0], "transition"), int64_t(0));

    NETW_CHECK_EQ(rows_of(owner, EventPlane::PREDICT_CONSUME).size(), 0);

    const Vector<Dictionary> consumes
        = rows_of(authority, EventPlane::PREDICT_CONSUME);
    REQUIRE(consumes.size() > 0);
    NETW_CHECK_GE(detail_int(consumes[0], "buffer"), int64_t(0));

    int replayed = 0;
    for (int at = 0; at < consumes.size(); ++at) {
        replayed += detail_int(consumes[at], "action")
                == int64_t(NetwPredict::CONSUME_ACTION_REPLAY)
            ? 1
            : 0;
    }
    NETW_CHECK_GT(replayed, 0);
}

TEST_CASE(
    "[Networked][LagComp][Episode] EP3 a comparison reports what it "
    "judged whether or not it corrected, and a recovery reports every plan it "
    "staged including the ones that declined to write, so a correction that "
    "never happened is still a row"
) {
    const Scenario scenario = watched_lane(true);
    LoopbackRig rig(scenario.clients);
    const CallLog log;
    watch(rig.client(0), stage_events(), log, "owner");

    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    const Vector<Dictionary> owner = reported(log, "owner");

    const Vector<Dictionary> judged
        = rows_of(owner, EventPlane::PREDICT_EVALUATE);
    NETW_CHECK_GT(judged.size(), 1);
    int uncorrected = 0;
    for (int at = 0; at < judged.size(); ++at) {
        uncorrected
            += bool(row_detail(judged[at]).get(StringName("corrected"), true))
            ? 0
            : 1;
    }
    NETW_CHECK_GT(uncorrected, 0);

    const Vector<Dictionary> staged
        = rows_of(owner, EventPlane::PREDICT_RECOVER);
    NETW_CHECK_GT(staged.size(), 0);
    int wrote = 0;
    for (int at = 0; at < staged.size(); ++at) {
        wrote += bool(row_detail(staged[at]).get(StringName("skip"), true)) ? 0
                                                                            : 1;
    }
    NETW_CHECK_GT(wrote, 0);
}

} // namespace TestNetwLagCompEpisodeReportLaws

#endif
