#include "support/netw_test.h"

#include "netw/api/clock_handle.hpp"
#include "netw/display_timing.hpp"

namespace TestNetwDisplayTiming {

using namespace godot;
using netw::NetwClockHandle;
using netw::NetwDisplayTiming;

Ref<NetwClockHandle> make_clock(
    int p_tickrate,
    int p_display_offset,
    int p_tick
) {
    Ref<NetwClockHandle> clock;
    clock.instantiate();
    clock->set_tickrate(p_tickrate);
    clock->set_display_offset(p_display_offset);
    clock->set_tick(p_tick);
    return clock;
}

TEST_CASE(
    "[Networked][Display][Hosted] DT1 a captured snapshot carries every scalar "
    "the clock answers, so no runtime ever needs the clock"
) {
    Ref<NetwClockHandle> clock = make_clock(30, 2, 100);
    clock->set_tick_factor_override(0.25);

    Ref<NetwDisplayTiming> timing = NetwDisplayTiming::capture(clock, 0.5);

    CHECK(timing->get_tick() == clock->get_tick());
    CHECK(timing->get_display_tick() == clock->get_display_tick());
    CHECK(
        timing->get_tick_factor()
        == doctest::Approx(clock->get_tick_factor())
    );
    CHECK(timing->get_ticktime() == doctest::Approx(clock->get_ticktime()));
    CHECK(timing->get_display_offset() == clock->get_display_offset());
    CHECK(
        timing->get_recommended_display_offset()
        == clock->get_recommended_display_offset()
    );
    CHECK(timing->get_frame_delta() == doctest::Approx(0.5));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT2 the display tick trails the simulation "
    "tick by the display offset, which is the buffer the smoothing spends"
) {
    Ref<NetwClockHandle> clock = make_clock(30, 2, 100);

    Ref<NetwDisplayTiming> timing = NetwDisplayTiming::capture(clock, 0.0);

    CHECK(timing->get_tick() == 100);
    CHECK(timing->get_display_tick() == 98);
    CHECK(timing->get_tick() - timing->get_display_tick() == 2);
}

TEST_CASE(
    "[Networked][Display][Hosted] DT3 frame_ticks scales the wall-clock delta "
    "by the TICKRATE, so it is the tick-space length of the same frame"
) {
    Ref<NetwClockHandle> clock = make_clock(30, 0, 0);

    Ref<NetwDisplayTiming> timing = NetwDisplayTiming::capture(clock, 0.5);

    CHECK(timing->get_frame_ticks() == doctest::Approx(15.0));
    CHECK(timing->get_frame_delta() == doctest::Approx(0.5));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT4 a capture with no clock is a zeroed "
    "snapshot rather than a refusal, so a pump before any configurator is inert"
) {
    Ref<NetwDisplayTiming> timing
        = NetwDisplayTiming::capture(Ref<NetwClockHandle>(), 0.5);

    REQUIRE(timing.is_valid());
    CHECK(timing->get_tick() == 0);
    CHECK(timing->get_display_tick() == 0);
    CHECK(timing->get_tick_factor() == doctest::Approx(0.0));
    CHECK(timing->get_ticktime() == doctest::Approx(0.0));
    CHECK(timing->get_display_offset() == 0);
    CHECK(timing->get_recommended_display_offset() == 0);
    CHECK(timing->get_frame_delta() == doctest::Approx(0.0));
    CHECK(timing->get_frame_ticks() == doctest::Approx(0.0));
}

TEST_CASE(
    "[Networked][Display][Hosted] DT5 a snapshot is a plain value a rig writes "
    "directly, so a timeline can be declared without standing a clock up"
) {
    Ref<NetwDisplayTiming> timing;
    timing.instantiate();

    timing->set_display_tick(42);
    timing->set_tick_factor(0.75);
    timing->set_ticktime(1.0 / 60.0);
    timing->set_frame_delta(1.0 / 120.0);

    CHECK(timing->get_display_tick() == 42);
    CHECK(timing->get_tick_factor() == doctest::Approx(0.75));
    CHECK(timing->get_ticktime() == doctest::Approx(1.0 / 60.0));
    CHECK(timing->get_frame_delta() == doctest::Approx(1.0 / 120.0));
}

} // namespace TestNetwDisplayTiming
