#include "netw/display/playhead.hpp"

#include <algorithm>
#include <cmath>

using namespace godot;

namespace netw::display {

double Playhead::min_lag(
    const Decl &decl,
    int display_offset,
    int recommended_display_offset
) const {
    if (decl.timeline_mode == TIMELINE_FORECAST) {
        return 0.0;
    }
    const double needed = double(expected_interval_ticks + 1);
    const double padding
        = double(std::max(0, recommended_display_offset - display_offset));
    return std::max(0.0, needed - double(display_offset) + padding);
}

int Playhead::effective_tick(int clock_display_tick) const {
    return int(std::floor(double(clock_display_tick) - display_lag));
}

void Playhead::dilate(
    const Decl &decl,
    double frame_ticks,
    int display_offset,
    int recommended_display_offset,
    bool starving
) {
    const double raw_floor
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor += (raw_floor - smoothed_floor) * decl.floor_smoothing;

    starvation_ticks = starving ? starvation_ticks + 1 : 0;

    if (starvation_ticks >= decl.starvation_grace_frames) {
        display_lag = std::min(
            display_lag + frame_ticks * decl.starvation_growth,
            smoothed_floor + decl.max_extra_dilation
        );
    } else {
        display_lag += (smoothed_floor - display_lag) * decl.lag_adapt_rate;
    }
}

double Playhead::place(
    int clock_display_tick,
    double tick_factor,
    bool bracketed
) {
    if (bracketed) {
        display_lag = 0.0;
        display_tick = std::max(0, clock_display_tick - 1);
        return tick_factor;
    }
    const double time
        = (double(clock_display_tick) + tick_factor) - display_lag;
    display_tick = int(std::floor(time));
    return time - double(display_tick);
}

void Playhead::settle(
    const Decl &decl,
    int display_offset,
    int recommended_display_offset
) {
    const double target
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor = target;
    display_lag = target;
    starvation_ticks = 0;
}

} // namespace netw::display
