#pragma once

#include "netw/clock_engine.hpp"

namespace netw::display {

struct Timing {
    int tick = 0;
    int display_tick = 0;
    double tick_factor = 0.0;
    double ticktime = 0.0;
    int display_offset = 0;
    int recommended_display_offset = 0;
    double frame_delta = 0.0;
    double frame_ticks = 0.0;
};

Timing capture_timing(const ClockEngine *p_clock, double p_frame_delta);

} // namespace netw::display
