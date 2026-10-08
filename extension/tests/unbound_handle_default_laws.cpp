#include "support/netw_test.h"

#include "netw/api/clock.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/display.hpp"

namespace TestUnboundHandleDefaultLaws {

using namespace godot;
using netw::NetwClock;
using netw::NetwClockConfig;
using netw::NetwDisplay;

TEST_CASE(
    "[Networked][Display][Hosted] U1 an unbound display reads every "
    "declared default, because the handle answers before an entity binds it"
) {
    Ref<NetwDisplay> display;
    display.instantiate();

    CHECK(display->get_enable_smart_dilation());
    CHECK(display->get_floor_smoothing() == doctest::Approx(0.05));
    CHECK(display->get_lag_adapt_rate() == doctest::Approx(0.05));
    CHECK(display->get_max_forecast_ticks() == 6);
    CHECK(display->get_chase_glide_time() == doctest::Approx(0.15));
}

TEST_CASE(
    "[Networked][Clock][Hosted] U2 an unbound clock reads its config "
    "defaults, because the handle answers before a session owns it"
) {
    Ref<NetwClock> clock;
    clock.instantiate();
    Ref<NetwClockConfig> config;
    config.instantiate();

    CHECK(clock->get_display_offset() == config->get_display_offset());
    CHECK(clock->get_display_offset() == 2);
    CHECK(clock->get_ping_interval() == doctest::Approx(0.1));
    CHECK(clock->get_tickrate() == config->get_tickrate());
    CHECK(clock->get_tickrate() == 30);
}

} // namespace TestUnboundHandleDefaultLaws
