#include "netw/api/clock_handle.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

#define NETW_CLOCK_RW(m_name, m_variant) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, "value"), \
        &NetwClockHandle::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwClockHandle::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(m_variant, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

#define NETW_CLOCK_RO(m_name, m_variant) \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwClockHandle::get_##m_name \
    ); \
    ADD_PROPERTY(PropertyInfo(m_variant, #m_name), "", "get_" #m_name)

void NetwClockHandle::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwClockHandle",
        D_METHOD("pumps_for", "seconds", "rate"),
        &NetwClockHandle::pumps_for
    );

    NETW_CLOCK_RW(tickrate, Variant::INT);
    NETW_CLOCK_RW(max_ticks_per_frame, Variant::INT);
    NETW_CLOCK_RW(stall_threshold, Variant::FLOAT);
    NETW_CLOCK_RW(use_physics_interpolation, Variant::BOOL);
    NETW_CLOCK_RW(panic_snap_threshold, Variant::INT);
    NETW_CLOCK_RW(stretch_nudge_factor, Variant::FLOAT);
    NETW_CLOCK_RW(ping_interval, Variant::FLOAT);
    NETW_CLOCK_RW(lead_ticks, Variant::FLOAT);
    NETW_CLOCK_RW(display_offset, Variant::INT);
    NETW_CLOCK_RW(jitter_multiplier, Variant::FLOAT);
    NETW_CLOCK_RW(jitter_window, Variant::INT);
    NETW_CLOCK_RW(jitter_stability_threshold, Variant::FLOAT);
    NETW_CLOCK_RW(enable_drift_logging, Variant::BOOL);
    NETW_CLOCK_RW(tick, Variant::INT);
    NETW_CLOCK_RW(is_synchronized, Variant::BOOL);
    NETW_CLOCK_RW(is_configured, Variant::BOOL);
    NETW_CLOCK_RW(manual_tick, Variant::BOOL);
    NETW_CLOCK_RW(node_pumped, Variant::BOOL);
    NETW_CLOCK_RW(tick_factor_override, Variant::FLOAT);

    ClassDB::bind_method(
        D_METHOD("set_sync_mode", "value"),
        &NetwClockHandle::set_sync_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_sync_mode"),
        &NetwClockHandle::get_sync_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "sync_mode",
            PROPERTY_HINT_ENUM,
            "Snap,Stretch"
        ),
        "set_sync_mode",
        "get_sync_mode"
    );

    NETW_CLOCK_RO(ticktime, Variant::FLOAT);
    NETW_CLOCK_RO(tick_factor, Variant::FLOAT);
    NETW_CLOCK_RO(tick_phase, Variant::FLOAT);
    NETW_CLOCK_RO(tick_accumulator, Variant::FLOAT);
    NETW_CLOCK_RO(display_tick, Variant::INT);
    NETW_CLOCK_RO(physics_factor, Variant::FLOAT);
    NETW_CLOCK_RO(physics_steps_per_tick, Variant::INT);
    NETW_CLOCK_RO(is_simulating, Variant::BOOL);
    NETW_CLOCK_RO(is_gated, Variant::BOOL);
    NETW_CLOCK_RO(simulation_behind_count, Variant::INT);
    NETW_CLOCK_RO(rtt, Variant::FLOAT);
    NETW_CLOCK_RO(rtt_avg, Variant::FLOAT);
    NETW_CLOCK_RO(rtt_jitter, Variant::FLOAT);
    NETW_CLOCK_RO(one_way_latency, Variant::FLOAT);
    NETW_CLOCK_RO(recommended_display_offset, Variant::INT);
    NETW_CLOCK_RO(is_stable, Variant::BOOL);

    ClassDB::bind_method(
        D_METHOD("physics_step", "delta"),
        &NetwClockHandle::physics_step
    );
    ClassDB::bind_method(
        D_METHOD("force_step", "count"),
        &NetwClockHandle::force_step
    );
    ClassDB::bind_method(
        D_METHOD("begin_tick_loop"),
        &NetwClockHandle::begin_tick_loop
    );
    ClassDB::bind_method(
        D_METHOD("end_tick_loop"),
        &NetwClockHandle::end_tick_loop
    );
    ClassDB::bind_method(
        D_METHOD("poll_step"),
        &NetwClockHandle::poll_step
    );
    ClassDB::bind_method(
        D_METHOD("count_poll"),
        &NetwClockHandle::count_poll
    );
    ClassDB::bind_method(D_METHOD("arm_gate"), &NetwClockHandle::arm_gate);
    ClassDB::bind_method(
        D_METHOD("release_gate"),
        &NetwClockHandle::release_gate
    );
    ClassDB::bind_method(
        D_METHOD("mark_step", "seconds_ago"),
        &NetwClockHandle::mark_step,
        DEFVAL(0.0)
    );
    ClassDB::bind_method(
        D_METHOD("seconds_since_step"),
        &NetwClockHandle::seconds_since_step
    );
    ClassDB::bind_method(
        D_METHOD("consume_ping_due", "delta"),
        &NetwClockHandle::consume_ping_due
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwClockHandle::clear);
    ClassDB::bind_method(D_METHOD("cadence"), &NetwClockHandle::cadence);
    ClassDB::bind_method(
        D_METHOD(
            "handle_pong",
            "sample",
            "server_tick_at_pong",
            "server_tick_phase",
            "apply_lead"
        ),
        &NetwClockHandle::handle_pong
    );

    BIND_ENUM_CONSTANT(SYNC_SNAP);
    BIND_ENUM_CONSTANT(SYNC_STRETCH);
}

#undef NETW_CLOCK_RW
#undef NETW_CLOCK_RO

} // namespace netw
