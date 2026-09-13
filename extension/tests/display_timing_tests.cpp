#include "support/netw_test.h"

#include "netw/clock_engine.hpp"
#include "netw/display/timing.hpp"

namespace TestNetwTiming {

using namespace godot;
using netw::ClockEngine;
using netw::display::capture_timing;
using netw::display::Timing;

ClockEngine make_clock(int p_tickrate, int p_display_offset, int p_tick) {
    ClockEngine clock;
    clock.set_tickrate(p_tickrate);
    clock.set_display_offset(p_display_offset);
    clock.set_tick(p_tick);
    return clock;
}

TEST_CASE(
    "[Networked][Display][Hosted] DT1 a captured snapshot carries every scalar "
    "the clock answers, so no runtime ever needs the clock"
) {
    ClockEngine clock = make_clock(30, 2, 100);
    clock.set_tick_factor_override(0.25);

    const Timing timing = capture_timing(&clock, 0.5);

    CHECK(timing.tick == clock.get_tick());
    CHECK(timing.display_tick == clock.display_tick());
    CHECK(timing.tick_factor == doctest::Approx(clock.tick_factor()));
    CHECK(timing.ticktime == doctest::Approx(clock.ticktime()));
    CHECK(timing.display_offset == clock.get_display_offset());
    CHECK(
        timing.recommended_display_offset == clock.recommended_display_offset()
    );
    CHECK(timing.frame_delta == doctest::Approx(0.5));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT2 the display tick trails the simulation "
    "tick by the display offset, which is the buffer the smoothing spends"
) {
    ClockEngine clock = make_clock(30, 2, 100);

    const Timing timing = capture_timing(&clock, 0.0);

    CHECK(timing.tick == 100);
    CHECK(timing.display_tick == 98);
    CHECK(timing.tick - timing.display_tick == 2);
}

TEST_CASE(
    "[Networked][Display][Hosted] DT3 frame_ticks scales the wall-clock delta "
    "by the TICKRATE, so it is the tick-space length of the same frame"
) {
    ClockEngine clock = make_clock(30, 0, 0);

    const Timing timing = capture_timing(&clock, 0.5);

    CHECK(timing.frame_ticks == doctest::Approx(15.0));
    CHECK(timing.frame_delta == doctest::Approx(0.5));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT4 a capture with no clock is a zeroed "
    "snapshot rather than a refusal, so a pump before any configurator is inert"
) {
    const Timing timing = capture_timing(nullptr, 0.5);

    CHECK(timing.tick == 0);
    CHECK(timing.display_tick == 0);
    CHECK(timing.tick_factor == doctest::Approx(0.0));
    CHECK(timing.ticktime == doctest::Approx(0.0));
    CHECK(timing.display_offset == 0);
    CHECK(timing.recommended_display_offset == 0);
    CHECK(timing.frame_delta == doctest::Approx(0.0));
    CHECK(timing.frame_ticks == doctest::Approx(0.0));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT5 a snapshot is a plain value a rig writes "
    "directly, so a timeline can be declared without standing a clock up"
) {
    Timing timing;

    timing.display_tick = 42;
    timing.tick_factor = 0.75;
    timing.ticktime = 1.0 / 60.0;
    timing.frame_delta = 1.0 / 120.0;

    CHECK(timing.display_tick == 42);
    CHECK(timing.tick_factor == doctest::Approx(0.75));
    CHECK(timing.ticktime == doctest::Approx(1.0 / 60.0));
    CHECK(timing.frame_delta == doctest::Approx(1.0 / 120.0));
}

} // namespace TestNetwTiming
