#include "support/netw_test.h"

#include "godot/engine.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/clock_engine.hpp"
#include "support/netw_recorder.h"

namespace TestNetwClockEngine {

using namespace godot;
using netw::ClockEngine;
using netw::NetwMultiplayer;
using netw_test::Recorder;

constexpr int EXACT_RATE = 8;
constexpr double TICK = 0.125;
constexpr double HALF_TICK = 0.0625;
constexpr double AN_HOUR = 3600.0;

struct Clocked {
    Ref<NetwMultiplayer> session;
    ClockEngine *clock = nullptr;

    ClockEngine *operator->() const {
        return clock;
    }
    Object *sink() const {
        return session.ptr();
    }
};

Clocked make_clock(int rate = EXACT_RATE) {
    Clocked out;
    out.session.instantiate();
    out.clock = &out.session->clock_engine();
    out.clock->set_tickrate(rate);
    return out;
}

Clocked pinned_clock() {
    Clocked out = make_clock();
    out->set_configured(true);
    out->set_use_physics_interpolation(false);
    return out;
}

Vector<bool> admitted(
    const Clocked &p_clock,
    const Vector<int> &p_ticks_per_frame
) {
    Vector<bool> out;
    for (int index = 0; index < p_ticks_per_frame.size(); ++index) {
        p_clock->force_step(p_ticks_per_frame[index]);
        out.push_back(p_clock->is_simulating());
    }
    return out;
}

TEST_CASE(
    "[Networked][Clock][Hosted] L1 the schedule keeps what a short frame could "
    "not spend"
) {
    const Clocked clock = make_clock();

    clock->physics_step(HALF_TICK);
    NETW_CHECK_EQ(clock->get_tick(), 0);

    clock->physics_step(HALF_TICK);
    NETW_CHECK_EQ(clock->get_tick(), 1);

    SUBCASE("a whole frame emits a whole tick") {
        clock->physics_step(TICK);
        NETW_CHECK_EQ(clock->get_tick(), 2);
    }

    SUBCASE("the loop brackets every frame, tick or not") {
        Recorder recorder(
            clock.sink(),
            {"clock_before_tick_loop",
             "clock_before_tick",
             "clock_on_tick",
             "clock_after_tick",
             "clock_after_tick_loop"}
        );
        clock->physics_step(TICK);
        CHECK(
            recorder.order()
            == Vector<StringName>(
                {"clock_before_tick_loop",
                 "clock_before_tick",
                 "clock_on_tick",
                 "clock_after_tick",
                 "clock_after_tick_loop"}
            )
        );

        recorder.clear();
        clock->physics_step(0.0);
        CHECK(
            recorder.order()
            == Vector<StringName>(
                {"clock_before_tick_loop", "clock_after_tick_loop"}
            )
        );
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L2 the ceiling caps a long frame and keeps the "
    "rest"
) {
    const Clocked clock = make_clock();
    clock->set_max_ticks_per_frame(2);

    clock->physics_step(TICK * 5.0);
    NETW_CHECK_EQ(clock->get_tick(), 2);

    SUBCASE(
        "the residue is still owed, so an empty frame keeps drawing on it"
    ) {
        clock->physics_step(0.0);
        NETW_CHECK_EQ(clock->get_tick(), 4);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L3 a stalling frame discards what was banked "
    "before it"
) {
    const Clocked kept = make_clock();
    kept->set_max_ticks_per_frame(100);
    kept->set_stall_threshold(TICK * 6.0);
    kept->physics_step(HALF_TICK);
    kept->physics_step(TICK * 5.0);

    const Clocked discarded = make_clock();
    discarded->set_max_ticks_per_frame(100);
    discarded->set_stall_threshold(TICK * 4.0);
    discarded->physics_step(HALF_TICK);
    discarded->physics_step(TICK * 5.0);

    NETW_CHECK_EQ(kept->get_tick(), 5);
    NETW_CHECK_EQ(discarded->get_tick(), 5);

    kept->physics_step(HALF_TICK);
    discarded->physics_step(HALF_TICK);

    NETW_CHECK_EQ(kept->get_tick(), 6);
    NETW_CHECK_EQ(discarded->get_tick(), 5);
}

TEST_CASE(
    "[Networked][Clock][Hosted] L4 the stepped and pumped paths emit the same "
    "schedule"
) {
    const Clocked stepped = make_clock();
    const Clocked pumped = make_clock();

    Recorder on_stepped(stepped.sink(), {"clock_on_tick"});
    Recorder on_pumped(pumped.sink(), {"clock_on_tick"});

    stepped->force_step(3);
    pumped->physics_step(TICK * 3.0);

    NETW_CHECK_EQ(stepped->get_tick(), pumped->get_tick());
    NETW_CHECK_EQ(
        on_stepped.count("clock_on_tick"),
        on_pumped.count("clock_on_tick")
    );
    NETW_CHECK_EQ(on_stepped.count("clock_on_tick"), 3);

    SUBCASE("a step is not a frame, so it brackets no tick loop") {
        Recorder brackets(
            stepped.sink(),
            {"clock_before_tick_loop", "clock_after_tick_loop"}
        );
        stepped->force_step(1);
        NETW_CHECK_EQ(brackets.count("clock_before_tick_loop"), 0);
        NETW_CHECK_EQ(brackets.count("clock_after_tick_loop"), 0);
    }

    SUBCASE("stepping zero advances nothing") {
        const int before = stepped->get_tick();
        stepped->force_step(0);
        NETW_CHECK_EQ(stepped->get_tick(), before);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L5 an ungated clock simulates every frame"
) {
    const Clocked clock = make_clock(60);

    CHECK(
        admitted(clock, Vector<int>({1, 0, 1, 0, 0, 1}))
        == Vector<bool>({true, true, true, true, true, true})
    );

    SUBCASE("and it never reports falling behind") {
        clock->physics_step(clock->ticktime() * 3.0);
        NETW_CHECK_EQ(clock->get_simulation_behind_count(), 0);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L6 a gated clock holds the frames that run no "
    "tick"
) {
    const Clocked clock = make_clock(60);
    clock->arm_gate();

    CHECK(
        admitted(clock, Vector<int>({1, 0, 1, 1, 0, 1}))
        == Vector<bool>({true, false, true, true, false, true})
    );

    SUBCASE("releasing the last gate gives the world its frames back") {
        clock->force_step(0);
        CHECK_FALSE(clock->is_simulating());

        clock->release_gate();
        CHECK_FALSE(clock->is_gated());
        CHECK(clock->is_simulating());
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L7 a tick worth two steps admits two frames"
) {
    const godot::Engine *engine = godot::Engine::get_singleton();
    REQUIRE(engine != nullptr);
    if (engine == nullptr) {
        return;
    }
    const Clocked clock
        = make_clock(engine->get_physics_ticks_per_second() / 2);
    NETW_CHECK_EQ(clock->physics_steps_per_tick(), 2);
    clock->arm_gate();

    CHECK(
        admitted(clock, Vector<int>({1, 0, 1, 0, 0, 1}))
        == Vector<bool>({true, true, true, true, false, true})
    );
}

TEST_CASE(
    "[Networked][Clock][Hosted] L8 gates nest and only the last release "
    "ungates"
) {
    const Clocked clock = make_clock(60);
    clock->arm_gate();
    clock->arm_gate();

    clock->release_gate();
    CHECK(clock->is_gated());
    clock->force_step(0);
    CHECK_FALSE(clock->is_simulating());

    clock->release_gate();
    CHECK_FALSE(clock->is_gated());
    CHECK(clock->is_simulating());

    SUBCASE("releasing past zero is not a negative gate") {
        clock->release_gate();
        CHECK_FALSE(clock->is_gated());
        clock->force_step(0);
        CHECK(clock->is_simulating());
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L9 a peer that cannot keep up reports it "
    "instead of doubling"
) {
    const Clocked clock = make_clock(60);
    clock->arm_gate();

    clock->physics_step(clock->ticktime() * 3.0);

    CHECK(clock->get_simulation_behind_count() > 0);
    CHECK(clock->is_simulating());
}

TEST_CASE(
    "[Networked][Clock][Hosted] L10 the derived readings follow the rate"
) {
    for (const int rate : {20, 60}) {
        const Clocked clock = make_clock(rate);
        CHECK(std::fabs(clock->ticktime() - 1.0 / double(rate)) < 0.0001);
    }

    struct Row {
        int tick;
        int offset;
        int shown;
    };
    for (const Row &row : {Row{10, 0, 10}, Row{10, 3, 7}, Row{2, 5, 0}}) {
        const Clocked clock = make_clock();
        clock->set_display_offset(row.offset);
        clock->set_tick(row.tick);
        NETW_CHECK_EQ(clock->display_tick(), row.shown);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L11 snap takes the whole correction where "
    "stretch takes a fraction"
) {
    const Clocked snap = make_clock(30);
    snap->set_sync_mode(ClockEngine::SYNC_SNAP);
    snap->set_lead_ticks(0.0);
    snap->handle_pong(0.0, 100, 0.0, true);
    snap->handle_pong(0.0, 150, 0.0, true);
    NETW_CHECK_EQ(snap->get_tick(), 150);

    const Clocked stretch = make_clock(30);
    stretch->set_sync_mode(ClockEngine::SYNC_STRETCH);
    stretch->set_lead_ticks(0.0);
    stretch->set_stretch_nudge_factor(0.5);
    stretch->set_panic_snap_threshold(100);
    stretch->handle_pong(0.0, 100, 0.0, true);
    stretch->handle_pong(0.0, 150, 0.0, true);

    NETW_CHECK_EQ(stretch->get_tick(), 100);
    stretch->physics_step(0.0);
    CHECK(stretch->get_tick() > 100);
    CHECK(stretch->get_tick() < 150);

    SUBCASE("a divergence past the panic threshold snaps rather than crawls") {
        stretch->set_panic_snap_threshold(2);
        stretch->handle_pong(0.0, 400, 0.0, true);
        stretch->physics_step(0.0);
        NETW_CHECK_EQ(stretch->get_tick(), 400);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L12 the first calibration announces itself "
    "once and seeds the phase"
) {
    const Clocked clock = make_clock(EXACT_RATE);
    clock->set_sync_mode(ClockEngine::SYNC_SNAP);
    clock->set_lead_ticks(0.0);
    Recorder recorder(clock.sink(), {"clock_synchronized"});

    CHECK_FALSE(clock->get_synchronized());

    clock->handle_pong(0.0, 40, 0.5, true);

    CHECK(clock->get_synchronized());
    NETW_CHECK_EQ(clock->get_tick(), 40);
    CHECK(std::fabs(clock->tick_phase() - 0.5) < 0.0001);

    SUBCASE("a later pong does not re-announce a synchronization") {
        clock->handle_pong(0.0, 41, 0.5, true);
        NETW_CHECK_EQ(recorder.count("clock_synchronized"), 1);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L13 the calibration target follows the "
    "server's phase continuously"
) {
    const double quarter = 1.0 / 32.0 / 4.0;
    Vector<int> emitted_at;
    for (const double phase : {0.0, 0.25, 0.5, 0.75}) {
        const Clocked clock = make_clock(32);
        clock->set_sync_mode(ClockEngine::SYNC_SNAP);
        clock->set_lead_ticks(1.0);
        clock->handle_pong(0.0, 100, phase, true);
        NETW_CHECK_EQ(clock->get_tick(), 101);

        int steps_until_tick = 0;
        for (int step = 0; step < 4; ++step) {
            clock->physics_step(quarter);
            if (clock->get_tick() == 101) {
                steps_until_tick += 1;
            }
        }
        emitted_at.push_back(steps_until_tick);
    }

    CHECK(emitted_at == Vector<int>({3, 2, 1, 0}));
}

TEST_CASE(
    "[Networked][Clock][Hosted] L14 jitter decides stability and widens the "
    "recommendation"
) {
    const Clocked clock = make_clock(30);
    clock->set_jitter_window(4);
    clock->set_jitter_stability_threshold(0.01);
    clock->set_jitter_multiplier(2.0);
    clock->set_display_offset(1);
    Recorder recorder(clock.sink(), {"clock_stability_changed"});

    clock->handle_pong(0.02, 100, 0.0, true);
    clock->handle_pong(0.02, 100, 0.0, true);
    CHECK(clock->is_stable());
    NETW_CHECK_EQ(recorder.count("clock_stability_changed"), 0);

    clock->handle_pong(0.2, 100, 0.0, true);
    CHECK_FALSE(clock->is_stable());
    NETW_CHECK_EQ(recorder.count("clock_stability_changed"), 1);
    CHECK(clock->recommended_display_offset() > 1);

    SUBCASE("the window forgets, so the outlier ages out of the average") {
        const double spiked = clock->rtt_avg();
        for (int index = 0; index < 4; ++index) {
            clock->handle_pong(0.02, 100, 0.0, true);
        }
        CHECK(clock->rtt_avg() < spiked);
        CHECK(clock->is_stable());
    }

    SUBCASE("an unsynchronized clock recommends what it was configured with") {
        const Clocked fresh = make_clock(30);
        fresh->set_display_offset(4);
        NETW_CHECK_EQ(fresh->recommended_display_offset(), 4);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L15 the tick factor is where a display sits "
    "inside the tick"
) {
    const Clocked clock = pinned_clock();
    clock->handle_pong(0.0, 40, 0.5, false);

    NETW_CHECK_CLOSE(clock->tick_factor(), 0.5, 0.0);

    SUBCASE("an unconfigured clock reads zero however much it banked") {
        clock->set_configured(false);
        NETW_CHECK_CLOSE(clock->tick_factor(), 0.0, 0.0);
    }

    SUBCASE("the override answers instead of any reading") {
        clock->set_tick_factor_override(0.25);
        NETW_CHECK_CLOSE(clock->tick_factor(), 0.25, 0.0);

        clock->set_configured(false);
        NETW_CHECK_CLOSE(clock->tick_factor(), 0.25, 0.0);
    }

    SUBCASE("a negative override leaves the reading to the clock") {
        clock->set_tick_factor_override(-1.0);
        NETW_CHECK_CLOSE(clock->tick_factor(), 0.5, 0.0);
    }

    SUBCASE("the frame that has already elapsed counts toward the reading") {
        clock->mark_step(HALF_TICK);
        NETW_CHECK_CLOSE(clock->tick_factor(), 1.0, 0.001);
    }

    SUBCASE("a span longer than the process has been alive still measures") {
        clock->mark_step(AN_HOUR);
        NETW_CHECK_CLOSE(clock->seconds_since_step(), AN_HOUR, 0.001);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L17 the step stamp measures the span since "
    "the last step"
) {
    const Clocked clock = make_clock();

    const bool unstepped_has_no_span = clock->seconds_since_step() < 0.0;
    CHECK(unstepped_has_no_span);

    clock->physics_step(TICK);
    const bool a_step_stamps = clock->seconds_since_step() >= 0.0;
    CHECK(a_step_stamps);

    SUBCASE("a session restart forgets the stamp") {
        clock->clear();
        const bool cleared_has_no_span = clock->seconds_since_step() < 0.0;
        CHECK(cleared_has_no_span);
    }

    SUBCASE("a frame pumped elsewhere re-stamps without stepping") {
        const int before = clock->get_tick();
        clock->mark_step();
        NETW_CHECK_EQ(clock->get_tick(), before);
        const bool marked_has_span = clock->seconds_since_step() >= 0.0;
        CHECK(marked_has_span);
    }
}

TEST_CASE(
    "[Networked][Clock][Hosted] L16 the tick factor is not clamped where the "
    "phase is"
) {
    const Clocked clock = pinned_clock();
    clock->set_max_ticks_per_frame(1);

    clock->physics_step(TICK * 3.0);

    NETW_CHECK_EQ(clock->get_tick(), 1);
    NETW_CHECK_CLOSE(clock->tick_phase(), 1.0, 0.0);
    NETW_CHECK_CLOSE(clock->tick_factor(), 2.0, 0.01);
    const bool above_the_phase = clock->tick_factor() > clock->tick_phase();
    CHECK(above_the_phase);
}

TEST_CASE("[Networked][Clock][Hosted] clear restarts the session") {
    const Clocked clock = make_clock();
    clock->arm_gate();
    clock->handle_pong(0.05, 100, 0.5, true);
    clock->physics_step(TICK);

    clock->clear();

    NETW_CHECK_EQ(clock->get_tick(), 0);
    CHECK_FALSE(clock->get_synchronized());
    CHECK_FALSE(clock->is_gated());
    CHECK(clock->is_simulating());
    NETW_CHECK_EQ(clock->get_simulation_behind_count(), 0);
    CHECK(std::fabs(clock->rtt_avg()) < 0.0000001);
    CHECK(std::fabs(clock->tick_phase()) < 0.0000001);
}

TEST_CASE(
    "[Networked][Clock][Hosted] a window in pumps rounds up, so a wait "
    "is never shorter than the seconds it was asked for"
) {
    NETW_CHECK_EQ(ClockEngine::pumps_for(2.0, 30.0), int64_t(60));
    NETW_CHECK_EQ(ClockEngine::pumps_for(0.05, 30.0), int64_t(2));
    NETW_CHECK_EQ(ClockEngine::pumps_for(0.5, 60.0), int64_t(30));
    NETW_CHECK_EQ(ClockEngine::pumps_for(0.0, 30.0), int64_t(0));
}

TEST_CASE(
    "[Networked][Clock][Hosted] a rate below one pump a second counts "
    "as one, so an unconfigured clock still expires a wait"
) {
    NETW_CHECK_EQ(ClockEngine::pumps_for(3.0, 0.0), int64_t(3));
    NETW_CHECK_EQ(ClockEngine::pumps_for(3.0, 0.25), int64_t(3));
    NETW_CHECK_EQ(ClockEngine::pumps_for(3.0, -30.0), int64_t(3));
}

TEST_CASE(
    "[Networked][Clock][Hosted] the tick loop bracket is a verb, so a hand "
    "driven frame opens and closes exactly as a pumped one does"
) {
    const Clocked clock = pinned_clock();
    Recorder recorder(
        clock.sink(),
        {"clock_before_tick_loop",
         "clock_before_tick",
         "clock_on_tick",
         "clock_after_tick",
         "clock_after_tick_loop"}
    );

    clock->begin_tick_loop();
    clock->force_step(1);
    clock->end_tick_loop();

    CHECK(
        recorder.order()
        == Vector<StringName>(
            {"clock_before_tick_loop",
             "clock_before_tick",
             "clock_on_tick",
             "clock_after_tick",
             "clock_after_tick_loop"}
        )
    );
}

TEST_CASE(
    "[Networked][Clock][Hosted] a bracket emits its own edge and nothing "
    "else, so a frame that ran no tick still costs one open and one close"
) {
    const Clocked clock = pinned_clock();
    Recorder recorder(
        clock.sink(),
        {"clock_before_tick_loop", "clock_after_tick_loop", "clock_on_tick"}
    );
    const int before = clock->get_tick();

    clock->begin_tick_loop();
    clock->end_tick_loop();

    NETW_CHECK_EQ(recorder.count("clock_before_tick_loop"), 1);
    NETW_CHECK_EQ(recorder.count("clock_after_tick_loop"), 1);
    NETW_CHECK_EQ(recorder.count("clock_on_tick"), 0);
    NETW_CHECK_EQ(clock->get_tick(), before);
}

TEST_CASE(
    "[Networked][Clock][Hosted] stepping still refuses to bracket itself, so "
    "a rig that owns the frame is the only thing that opens one"
) {
    const Clocked clock = pinned_clock();
    Recorder recorder(
        clock.sink(),
        {"clock_before_tick_loop", "clock_after_tick_loop", "clock_on_tick"}
    );

    clock->force_step(2);

    NETW_CHECK_EQ(recorder.count("clock_on_tick"), 2);
    NETW_CHECK_EQ(recorder.count("clock_before_tick_loop"), 0);
    NETW_CHECK_EQ(recorder.count("clock_after_tick_loop"), 0);
}

TEST_CASE(
    "[Networked][Clock][Hosted] an unpumped clock reports an empty cadence "
    "rather than a stale window"
) {
    const Clocked clock = pinned_clock();

    const Dictionary idle = clock->cadence();

    NETW_CHECK_EQ(int(idle[StringName("physics_frames")]), 0);
    NETW_CHECK_EQ(int(idle[StringName("polls")]), 0);
    NETW_CHECK_EQ(double(idle[StringName("wall_seconds")]), 0.0);
    NETW_CHECK_EQ(double(idle[StringName("physics_hz")]), 0.0);
    NETW_CHECK_EQ(double(idle[StringName("poll_hz")]), 0.0);
}

TEST_CASE(
    "[Networked][Clock][Hosted] the two pumps are counted apart, and a "
    "deterministic step counts as neither"
) {
    const Clocked clock = pinned_clock();

    clock->physics_step(TICK);
    clock->physics_step(TICK);
    clock->count_poll();
    clock->force_step(3);
    clock->begin_tick_loop();
    clock->end_tick_loop();

    const Dictionary counted = clock->cadence();
    NETW_CHECK_EQ(int(counted[StringName("physics_frames")]), 2);
    NETW_CHECK_EQ(int(counted[StringName("polls")]), 1);
}

TEST_CASE(
    "[Networked][Clock][Hosted] the cadence span ends with the session, so a "
    "second one is not measured against the first one's origin"
) {
    const Clocked clock = pinned_clock();
    clock->physics_step(TICK);
    clock->count_poll();

    clock->clear();

    const Dictionary restarted = clock->cadence();
    NETW_CHECK_EQ(int(restarted[StringName("physics_frames")]), 0);
    NETW_CHECK_EQ(int(restarted[StringName("polls")]), 0);
    NETW_CHECK_EQ(double(restarted[StringName("wall_seconds")]), 0.0);
}

} // namespace TestNetwClockEngine
