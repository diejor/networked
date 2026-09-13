#include "netw/display/timing.hpp"

namespace netw::display {

Timing capture_timing(const ClockEngine *p_clock, double p_frame_delta) {
    Timing timing;
    if (p_clock == nullptr) {
        return timing;
    }
    timing.tick = p_clock->get_tick();
    timing.display_tick = p_clock->display_tick();
    timing.tick_factor = p_clock->tick_factor();
    timing.ticktime = p_clock->ticktime();
    timing.display_offset = p_clock->get_display_offset();
    timing.recommended_display_offset = p_clock->recommended_display_offset();
    timing.frame_delta = p_frame_delta;
    timing.frame_ticks = p_frame_delta * double(p_clock->get_tickrate());
    return timing;
}

} // namespace netw::display
