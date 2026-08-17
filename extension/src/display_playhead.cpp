#include "netw/display_playhead.hpp"

#include <algorithm>
#include <cmath>

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

double NetwDisplayPlayhead::min_lag(
    const Ref<NetwDisplayDecl> &decl,
    int display_offset,
    int recommended_display_offset
) const {
    if (decl.is_valid()
        && decl->get_timeline_mode() == NetwDisplayDecl::TIMELINE_FORECAST) {
        return 0.0;
    }
    const double needed = double(expected_interval_ticks + 1);
    const double padding
        = double(std::max(0, recommended_display_offset - display_offset));
    return std::max(0.0, needed - double(display_offset) + padding);
}

int NetwDisplayPlayhead::effective_tick(int clock_display_tick) const {
    return int(std::floor(double(clock_display_tick) - display_lag));
}

void NetwDisplayPlayhead::dilate(
    const Ref<NetwDisplayDecl> &decl,
    double frame_ticks,
    int display_offset,
    int recommended_display_offset,
    bool starving
) {
    NETW_ERR_COND(
        decl.is_null(),
        sys::INTERPOLATION,
        "NetwDisplayPlayhead.dilate: no display settings to dilate under."
    );
    const double raw_floor
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor
        += (raw_floor - smoothed_floor) * decl->get_floor_smoothing();

    starvation_ticks = starving ? starvation_ticks + 1 : 0;

    if (starvation_ticks >= decl->get_starvation_grace_frames()) {
        display_lag = std::min(
            display_lag + frame_ticks * decl->get_starvation_growth(),
            smoothed_floor + decl->get_max_extra_dilation()
        );
    } else {
        display_lag
            += (smoothed_floor - display_lag) * decl->get_lag_adapt_rate();
    }
}

double NetwDisplayPlayhead::place(
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

void NetwDisplayPlayhead::settle(
    const Ref<NetwDisplayDecl> &decl,
    int display_offset,
    int recommended_display_offset
) {
    const double target
        = min_lag(decl, display_offset, recommended_display_offset);
    smoothed_floor = target;
    display_lag = target;
    starvation_ticks = 0;
}

#define NETW_PLAYHEAD_FIELD(m_type, m_name)                                    \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwDisplayPlayhead::get_##m_name                                     \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwDisplayPlayhead::set_##m_name                                     \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

void NetwDisplayPlayhead::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("min_lag", "decl", "display_offset", "recommended_offset"),
        &NetwDisplayPlayhead::min_lag
    );
    ClassDB::bind_method(
        D_METHOD("effective_tick", "clock_display_tick"),
        &NetwDisplayPlayhead::effective_tick
    );
    ClassDB::bind_method(
        D_METHOD(
            "dilate",
            "decl",
            "frame_ticks",
            "display_offset",
            "recommended_offset",
            "starving"
        ),
        &NetwDisplayPlayhead::dilate
    );
    ClassDB::bind_method(
        D_METHOD("place", "clock_display_tick", "tick_factor", "bracketed"),
        &NetwDisplayPlayhead::place
    );
    ClassDB::bind_method(
        D_METHOD("settle", "decl", "display_offset", "recommended_offset"),
        &NetwDisplayPlayhead::settle
    );

    NETW_PLAYHEAD_FIELD(Variant::FLOAT, display_lag);
    NETW_PLAYHEAD_FIELD(Variant::FLOAT, smoothed_floor);
    NETW_PLAYHEAD_FIELD(Variant::INT, starvation_ticks);
    NETW_PLAYHEAD_FIELD(Variant::INT, expected_interval_ticks);
    NETW_PLAYHEAD_FIELD(Variant::INT, display_tick);
}

} // namespace netw
