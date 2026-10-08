#include "netw/display/playhead.hpp"

#include <algorithm>
#include <cmath>

using namespace godot;

namespace netw::display {

void Playhead::hear_newest(const Decl &decl, int64_t newest_tick, double now) {
    if (newest_tick <= newest_heard) {
        return;
    }
    const bool follows_a_heard_row = newest_heard >= 0;
    newest_heard = newest_tick;
    if (!follows_a_heard_row) {
        return;
    }
    arrival_age = now - double(newest_tick);
    if (!has_arrival_age) {
        has_arrival_age = true;
        arrival_age_mean = arrival_age;
        arrival_age_deviation = 0.0;
        return;
    }
    const double rate = decl.arrival_age_rate;
    arrival_age_deviation
        += (std::fabs(arrival_age - arrival_age_mean) - arrival_age_deviation)
        * rate;
    arrival_age_mean += (arrival_age - arrival_age_mean) * rate;
}

void Playhead::forget_arrivals() {
    newest_heard = -1;
    has_arrival_age = false;
    arrival_age = 0.0;
    arrival_age_mean = 0.0;
    arrival_age_deviation = 0.0;
}

double Playhead::arrival_floor(const Decl &decl) const {
    if (!has_arrival_age) {
        return 0.0;
    }
    const double spread
        = arrival_age_mean + decl.arrival_spread_weight * arrival_age_deviation;
    return std::clamp(spread + 1.0, 0.0, decl.max_arrival_lag);
}

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
    return std::max(
        std::max(0.0, needed - double(display_offset) + padding),
        arrival_floor(decl)
    );
}

int Playhead::effective_tick(int clock_display_tick, double tick_factor) const {
    return int(
        std::floor(double(clock_display_tick) + tick_factor - display_lag)
    );
}

void Playhead::dilate(
    const Decl &decl,
    int display_offset,
    int recommended_display_offset,
    bool starving
) {
    const double raw_floor
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor += (raw_floor - smoothed_floor) * decl.floor_smoothing;

    starvation_ticks = starving ? starvation_ticks + 1 : 0;

    display_lag += (smoothed_floor - display_lag) * decl.lag_adapt_rate;
}

double Playhead::place(
    int clock_display_tick,
    double tick_factor,
    bool bracketed
) {
    if (bracketed) {
        display_lag = 0.0;
        const double whole_ticks = std::floor(tick_factor);
        display_tick = std::max(0, clock_display_tick - 1 + int(whole_ticks));
        return tick_factor - whole_ticks;
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
    forget_arrivals();
    const double target
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor = target;
    display_lag = target;
    starvation_ticks = 0;
}

} // namespace netw::display
