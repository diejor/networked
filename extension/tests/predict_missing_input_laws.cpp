#include "support/netw_test.h"

#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/prediction.hpp"
#include "netw/predict/engine.hpp"
#include "support/installable_stepper.h"
#include "support/predict_rows.h"

namespace TestNetwPredictMissingInputLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::PackedInt32Array;
using godot::Ref;
using godot::StringName;
using godot::Vector2;
using netw::MissingInput;

constexpr int WARM_FRAMES = 30;
constexpr int DRAIN_FRAMES = 40;
constexpr int QUEUED = 6;

const Vector2 HELD_MOTION = Vector2(1.0, 0.0);

Scenario framed_lane(MissingInput p_policy, int p_window = -1) {
    Scenario scenario;
    scenario.label = "missing-input";
    scenario.epsilon = 0.01;
    scenario.world.clocked(60, 3).player(
        EntityDecl()
            .named("P")
            .on_schema("PredictedPose")
            .synced("position")
            .placed_at(Vector2())
            .predicted(0.01)
            .scheduled(netw::Schedule::FRAME)
            .missing_input(p_policy)
            .windowed(p_window),
        0
    );
    scenario.hold_input(1, "P", HELD_MOTION);
    PackedInt32Array ticks;
    for (int frame = 0; frame < WARM_FRAMES; ++frame) {
        ticks.push_back(1);
    }
    scenario.schedule(ticks);
    return scenario.until(int(ticks.size()));
}

struct Lane {
    LoopbackRig *rig = nullptr;
    netw::NetwPredictionEngine *pool = nullptr;
    int64_t slot = -1;
    int64_t hole = -1;

    int64_t fact(const char *p_name) const {
        const Ref<netw::NetwPredictStats> stats
            = rig->prediction_handle(StringName("P"))->get_stats();
        return int64_t(
            stats->to_dictionary().get(StringName(p_name), int64_t(-1))
        );
    }

    int64_t cursor() const {
        return pool->replay_cursor_of(slot);
    }

    int64_t ack() const {
        return pool->ack_of(slot);
    }

    bool admit(int64_t p_transition, const Vector2 &p_motion) {
        Dictionary held;
        held[StringName("motion")] = p_motion;
        return pool->command_admit(
            slot,
            p_transition,
            pool->last_replayed_label_of(slot) + 1 + (p_transition - cursor()),
            true,
            input_row(pool, slot, held)
        );
    }

    void queue_behind_hole() {
        for (int64_t queued = 1; queued <= QUEUED; ++queued) {
            REQUIRE(admit(hole + queued, HELD_MOTION));
        }
    }

    void step(int p_frames) {
        for (int frame = 0; frame < p_frames; ++frame) {
            rig->step_frame(1);
        }
    }

    godot::Node *body() const {
        return rig->node_of(rig->entity_of(StringName("P")));
    }

    Vector2 motion() const {
        return Vector2(body()->get("motion"));
    }

    double x() const {
        return double(Vector2(body()->get("position")).x);
    }

    netw::predict::JournalRow drove(int64_t p_transition) const {
        return pool->journal_row(slot, p_transition);
    }
};

Lane seat(LoopbackRig &p_rig) {
    Lane lane;
    lane.rig = &p_rig;
    lane.pool = p_rig.prediction_pool();
    lane.slot = p_rig.prediction_slot(StringName("P"));
    return lane;
}

Lane open_hole(LoopbackRig &p_rig) {
    Lane lane = seat(p_rig);
    const bool warmed = lane.fact("consumed") > 0;
    REQUIRE_MESSAGE(
        warmed,
        "a consumer that ran nothing before the hole proves nothing about it"
    );

    Ref<netw::LocalLinkConditions> silent = netw::LocalLinkConditions::create();
    silent->set_packet_loss(1.0);
    p_rig.conditions(0, silent);
    p_rig.conditions(-1, silent);

    int starved = 0;
    for (int frame = 0; frame < DRAIN_FRAMES && starved < 3; ++frame) {
        lane.step(1);
        if (!lane.pool->command_has(lane.slot, lane.cursor())) {
            starved += 1;
        }
    }
    const bool drained = starved >= 3;
    REQUIRE_MESSAGE(
        drained,
        "the consumer never ran out of the inputs it already held"
    );
    lane.hole = lane.cursor();
    const bool numbered = lane.hole > 0;
    REQUIRE(numbered);
    const int64_t starved_before = lane.fact("starved");
    lane.step(1);
    const bool one_frame = lane.fact("starved") == starved_before + 1;
    REQUIRE_MESSAGE(one_frame, "one rig step must be one consume frame");
    return lane;
}

void runs_the_hole_at_once(Lane &p_lane) {
    p_lane.queue_behind_hole();
    const int64_t consumed = p_lane.fact("consumed");
    const int64_t starved = p_lane.fact("starved");
    const int64_t missing = p_lane.fact("missing");

    p_lane.step(1);
    NETW_CHECK_EQ(p_lane.ack(), p_lane.hole);
    NETW_CHECK_EQ(p_lane.cursor(), p_lane.hole + 1);
    NETW_CHECK_EQ(p_lane.fact("consumed"), consumed + 1);
    for (int frame = 2; frame <= QUEUED + 1; ++frame) {
        p_lane.step(1);
        NETW_CHECK_EQ(p_lane.fact("consumed"), consumed + frame);
    }
    NETW_CHECK_EQ(p_lane.fact("starved"), starved);
    NETW_CHECK_EQ(p_lane.fact("missing"), missing + 1);
    NETW_CHECK_EQ(p_lane.cursor(), p_lane.hole + QUEUED + 1);
}

TEST_CASE(
    "[Networked][Predict][Lane] L1 a FRAME consumer runs a missing input in "
    "the frame its cursor reaches it when a later input is held, so it "
    "consumes every frame and never starves behind the hole"
) {
    const Scenario scenario = framed_lane(MissingInput::REPEAT_LAST);
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    Lane lane = open_hole(rig);
    runs_the_hole_at_once(lane);
}

TEST_CASE(
    "[Networked][Predict][Lane] L2 a missing FRAME input runs the declared "
    "missing policy, the input before it under REPEAT_LAST and the stall row "
    "written whole under STALL"
) {
    SUBCASE("REPEAT_LAST drives the input of the transition before the hole") {
        const Scenario scenario = framed_lane(MissingInput::REPEAT_LAST);
        LoopbackRig rig(scenario.clients);
        ScenarioRun::session(rig, scenario);
        Lane lane = open_hole(rig);
        lane.queue_behind_hole();
        const double before = lane.x();
        lane.step(1);
        NETW_REQUIRE_EQ(lane.cursor(), lane.hole + 1);
        REQUIRE(lane.drove(lane.hole).present);
        REQUIRE(lane.drove(lane.hole - 1).present);
        CHECK(bool(lane.motion() == HELD_MOTION));
        NETW_CHECK_GT(lane.x(), before);
        NETW_CHECK_EQ(
            lane.drove(lane.hole).c_hash,
            lane.drove(lane.hole - 1).c_hash
        );
    }
    SUBCASE("STALL drives the stall row and never the input before it") {
        const Scenario scenario = framed_lane(MissingInput::STALL);
        LoopbackRig rig(scenario.clients);
        ScenarioRun::session(rig, scenario);
        Lane lane = open_hole(rig);
        lane.queue_behind_hole();
        const double before = lane.x();
        lane.step(1);
        NETW_REQUIRE_EQ(lane.cursor(), lane.hole + 1);
        REQUIRE(lane.drove(lane.hole).present);
        REQUIRE(lane.drove(lane.hole - 1).present);
        CHECK(bool(lane.motion() == Vector2()));
        NETW_CHECK_CLOSE(lane.x(), before, 1e-9);
        CHECK(
            bool(
                lane.drove(lane.hole).c_hash != lane.drove(lane.hole - 1).c_hash
            )
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Lane] L5 a missing input inside replay_buffer_depth "
    "is run as missing rather than held, so consumption continues down to "
    "the buffer"
) {
    const Scenario scenario = framed_lane(MissingInput::REPEAT_LAST);
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    Lane lane = open_hole(rig);
    rig.prediction_handle(StringName("P"))->set_replay_buffer_depth(2);

    REQUIRE(lane.admit(lane.hole, HELD_MOTION));
    for (int64_t queued = 2; queued <= QUEUED + 1; ++queued) {
        REQUIRE(lane.admit(lane.hole + queued, HELD_MOTION));
    }
    const int64_t consumed = lane.fact("consumed");
    const int64_t missing = lane.fact("missing");

    lane.step(QUEUED + 2);

    NETW_CHECK_EQ(lane.fact("missing"), missing + 1);
    NETW_CHECK_EQ(lane.fact("consumed"), consumed + QUEUED);
    NETW_CHECK_EQ(lane.cursor(), lane.hole + QUEUED);
}

int64_t missing_after_silence(int p_window, int p_silenced) {
    const Scenario scenario = framed_lane(MissingInput::REPEAT_LAST, p_window);
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    Lane lane = seat(rig);
    const int64_t flowing = lane.fact("consumed");
    lane.step(4);
    const bool live = lane.fact("consumed") > flowing;
    REQUIRE_MESSAGE(live, "the owner's inputs stopped reaching the server");

    const int64_t missing = lane.fact("missing");
    Ref<netw::LocalLinkConditions> silent = netw::LocalLinkConditions::create();
    silent->set_packet_loss(1.0);
    rig.conditions(-1, silent);
    lane.step(p_silenced);
    rig.conditions(-1, Ref<netw::LocalLinkConditions>());
    lane.step(p_silenced + p_window + 8);
    return lane.fact("missing") - missing;
}

TEST_CASE(
    "[Networked][Predict][Lane] L6 windowed(n) repeats the newest n + 2 "
    "inputs in every command frame, so n + 1 lost frames in a row lose no "
    "input and n + 2 lose exactly one, which runs as missing"
) {
    for (const int window : {1, 3}) {
        NETW_FORMAT_INT(window_text, window);
        CAPTURE(window_text);
        NETW_CHECK_EQ(missing_after_silence(window, window + 1), 0);
        NETW_CHECK_EQ(missing_after_silence(window, window + 2), 1);
    }
}

TEST_CASE(
    "[Networked][Predict][Lane][Stepped] L7 a STEPPED consumer runs a missing "
    "input at once exactly as a FRAME consumer does"
) {
    const Scenario scenario = framed_lane(MissingInput::REPEAT_LAST);
    LoopbackRig rig(scenario.clients);
    ScenarioRun::session(rig, scenario);
    netw::NetwMultiplayer *const server = rig.server();
    const Ref<netw::NetwEntity> entity
        = rig.prediction_handle(StringName("P"))->get_entity();
    godot::Node *body = entity->get_owner();
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID space = server->entity_space_of(entity).space;
    REQUIRE(space.is_valid());
    Ref<InstallableStepper> stepper;
    stepper.instantiate();
    server->predict_stepper_install(space, stepper);
    entity->get_simulation()->set_schedule(
        netw::NetwSimulation::SCHEDULE_STEPPED
    );

    Lane lane = seat(rig);
    lane.step(WARM_FRAMES);
    NETW_CHECK_EQ(
        lane.pool->schedule_of(lane.slot),
        int(netw::NetwSimulation::SCHEDULE_STEPPED)
    );
    lane = open_hole(rig);
    runs_the_hole_at_once(lane);
    netw::gd::scene_root()->remove_child(body);
}

} // namespace TestNetwPredictMissingInputLaws

#endif
