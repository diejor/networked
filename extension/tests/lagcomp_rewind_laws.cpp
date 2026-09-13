#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/lagcomp_core.hpp"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwLagCompRewindLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::Ref;
using godot::StringName;
using godot::Vector2;
using netw::NetwLagCompCore;
using netw::NetwMultiplayer;

constexpr double HIT_RADIUS = 6.0;
constexpr int64_t PERCEIVED_AGO = 8;

EntityDecl moving_player() {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario rewound_lane() {
    Scenario scenario;
    scenario.label = "rewound-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(moving_player(), 0);
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    return scenario.until(60);
}

double x_of(const Dictionary &p_row) {
    return double(Vector2(p_row[StringName("position")]).x);
}

TEST_CASE(
    "[Networked][LagComp][Rewind] RW1 a shot aimed where the shooter "
    "SAW the target lands on the rewound history and misses the live body, "
    "so lag compensation is a query rather than a system"
) {
    const Scenario scenario = rewound_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    NetwLagCompCore *history = server->get_lagcomp_core();
    REQUIRE(history != nullptr);

    const godot::RID entity = rig.entity_of(StringName("P"));
    const Ref<godot::RefCounted> seated = server->entity_get_view(entity);
    REQUIRE(seated.is_valid());

    const int64_t perceived = server->clock_get_tick() - PERCEIVED_AGO;
    const Dictionary past = history->timeline_sample_entity(seated, perceived);
    REQUIRE_MESSAGE(
        !past.is_empty(),
        "the server recorded no authoritative history to rewind into"
    );

    godot::Node2D *body
        = godot::Object::cast_to<godot::Node2D>(rig.node_of(entity));
    REQUIRE(body != nullptr);
    const double live = double(body->get_position().x);
    const double rewound = x_of(past);

    NETW_CHECK_GT(live - rewound, HIT_RADIUS);

    SUBCASE("and the shot that hits history is the one that misses now") {
        CHECK(godot::Math::abs(rewound - rewound) <= HIT_RADIUS);
        CHECK(godot::Math::abs(rewound - live) > HIT_RADIUS);
    }
}

TEST_CASE(
    "[Networked][LagComp][Rewind] RW2 the history a rewind reads is "
    "recorded every tick by authority itself, so a tick inside the window "
    "answers rather than falling back to the oldest row"
) {
    const Scenario scenario = rewound_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    NetwLagCompCore *history = server->get_lagcomp_core();
    const godot::RID entity = rig.entity_of(StringName("P"));
    const Ref<godot::RefCounted> seated = server->entity_get_view(entity);
    REQUIRE(seated.is_valid());

    const int64_t now = server->clock_get_tick();
    double previous = 0.0;
    int answered = 0;
    for (int64_t ago = PERCEIVED_AGO; ago >= 1; --ago) {
        const Dictionary row
            = history->timeline_sample_entity(seated, now - ago);
        if (row.is_empty()) {
            continue;
        }
        const double here = x_of(row);
        if (answered > 0) {
            NETW_CHECK_GE(here, previous);
        }
        previous = here;
        answered += 1;
    }

    NETW_CHECK_GE(answered, int(PERCEIVED_AGO) / 2);
}

TEST_CASE(
    "[Networked][LagComp][Rewind] RW3 the session answers a sample as "
    "a DictionaryRecord carrying the row the history holds, and an entity "
    "with no seat answers an EMPTY snapshot rather than nothing at all"
) {
    const Scenario scenario = rewound_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    NetwLagCompCore *history = server->get_lagcomp_core();
    const godot::RID entity = rig.entity_of(StringName("P"));
    const Ref<godot::RefCounted> seated = server->entity_get_view(entity);
    REQUIRE(seated.is_valid());

    const int64_t perceived = server->clock_get_tick() - PERCEIVED_AGO;
    const Dictionary row = history->timeline_sample_entity(seated, perceived);
    REQUIRE(!row.is_empty());

    const Ref<netw::DictionaryRecord> past
        = server->lagcomp_sample(entity, perceived);
    REQUIRE(past.is_valid());
    CHECK(past->has_value(StringName("position")));
    NETW_CHECK_EQ(
        double(Vector2(past->get_value(StringName("position"))).x),
        x_of(row)
    );

    const Ref<netw::DictionaryRecord> by_wrapper
        = server->lagcomp_sample_of(seated, perceived);
    REQUIRE(by_wrapper.is_valid());
    NETW_CHECK_EQ(
        double(Vector2(by_wrapper->get_value(StringName("position"))).x),
        x_of(row)
    );

    SUBCASE("and an unseated handle is an empty snapshot, never a null one") {
        const Ref<netw::DictionaryRecord> absent
            = server->lagcomp_sample(godot::RID(), perceived);
        REQUIRE(absent.is_valid());
        CHECK(!absent->has_value(StringName("position")));
        NETW_CHECK_EQ(absent->get_property_names().size(), int64_t(0));

        Ref<godot::RefCounted> stranger;
        stranger.instantiate();
        const Ref<netw::DictionaryRecord> unseated
            = server->lagcomp_sample_of(stranger, perceived);
        REQUIRE(unseated.is_valid());
        NETW_CHECK_EQ(unseated->get_property_names().size(), int64_t(0));
    }

    SUBCASE(
        "and the sample is detached, so writing it never reaches the "
        "timeline the next sample reads"
    ) {
        const double held = x_of(row);
        const Ref<netw::DictionaryRecord> written
            = server->lagcomp_sample(entity, perceived);
        REQUIRE(written.is_valid());
        written->set_value(StringName("position"), Vector2(-9999.0, -9999.0));

        const Ref<netw::DictionaryRecord> reread
            = server->lagcomp_sample(entity, perceived);
        REQUIRE(reread.is_valid());
        NETW_CHECK_EQ(
            double(Vector2(reread->get_value(StringName("position"))).x),
            held
        );
        NETW_CHECK_EQ(
            double(Vector2(history->timeline_sample_entity(seated, perceived)
                               .get(StringName("position"), Vector2()))
                       .x),
            held
        );
    }
}

} // namespace TestNetwLagCompRewindLaws

#endif
