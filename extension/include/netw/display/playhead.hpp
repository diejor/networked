#pragma once

#include <cstdint>

#include "netw/display/decl.hpp"

namespace netw::display {

class Playhead {
private:
    double display_lag = 0.0;
    double smoothed_floor = 0.0;
    int32_t starvation_ticks = 0;
    int32_t expected_interval_ticks = 3;
    int32_t display_tick = -1;

public:
    double min_lag(
        const Decl &decl,
        int display_offset,
        int recommended_display_offset
    ) const;

    int effective_tick(int clock_display_tick) const;

    void dilate(
        const Decl &decl,
        double frame_ticks,
        int display_offset,
        int recommended_display_offset,
        bool starving
    );

    double place(int clock_display_tick, double tick_factor, bool bracketed);

    void settle(
        const Decl &decl,
        int display_offset,
        int recommended_display_offset
    );

    void set_display_lag(double value) {
        display_lag = value;
    }
    double get_display_lag() const {
        return display_lag;
    }
    void set_smoothed_floor(double value) {
        smoothed_floor = value;
    }
    double get_smoothed_floor() const {
        return smoothed_floor;
    }
    void set_starvation_ticks(int value) {
        starvation_ticks = value;
    }
    int get_starvation_ticks() const {
        return starvation_ticks;
    }
    void set_expected_interval_ticks(int value) {
        expected_interval_ticks = value;
    }
    int get_expected_interval_ticks() const {
        return expected_interval_ticks;
    }
    void set_display_tick(int value) {
        display_tick = value;
    }
    int get_display_tick() const {
        return display_tick;
    }
};

} // namespace netw::display
