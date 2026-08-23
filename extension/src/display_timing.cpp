#include "netw/display_timing.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<NetwDisplayTiming> NetwDisplayTiming::capture(
    const Ref<NetwClockHandle> &p_clock,
    double p_frame_delta
) {
    Ref<NetwDisplayTiming> timing;
    timing.instantiate();
    if (p_clock.is_null()) {
        return timing;
    }
    timing->tick = p_clock->get_tick();
    timing->display_tick = p_clock->get_display_tick();
    timing->tick_factor = p_clock->get_tick_factor();
    timing->ticktime = p_clock->get_ticktime();
    timing->display_offset = p_clock->get_display_offset();
    timing->recommended_display_offset
        = p_clock->get_recommended_display_offset();
    timing->frame_delta = p_frame_delta;
    timing->frame_ticks = p_frame_delta * double(p_clock->get_tickrate());
    return timing;
}

#define NETW_TIMING_FIELD(m_type, m_name)                                      \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwDisplayTiming::get_##m_name                                       \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwDisplayTiming::set_##m_name                                       \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

void NetwDisplayTiming::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwDisplayTiming",
        D_METHOD("capture", "clock", "frame_delta"),
        &NetwDisplayTiming::capture
    );
    NETW_TIMING_FIELD(Variant::INT, tick);
    NETW_TIMING_FIELD(Variant::INT, display_tick);
    NETW_TIMING_FIELD(Variant::FLOAT, tick_factor);
    NETW_TIMING_FIELD(Variant::FLOAT, ticktime);
    NETW_TIMING_FIELD(Variant::INT, display_offset);
    NETW_TIMING_FIELD(Variant::INT, recommended_display_offset);
    NETW_TIMING_FIELD(Variant::FLOAT, frame_delta);
    NETW_TIMING_FIELD(Variant::FLOAT, frame_ticks);
}

#undef NETW_TIMING_FIELD

} // namespace netw
