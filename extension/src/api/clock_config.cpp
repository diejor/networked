#include "netw/api/clock_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwClockConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_tickrate", "tickrate"),
        &NetwClockConfig::set_tickrate
    );
    ClassDB::bind_method(
        D_METHOD("get_tickrate"),
        &NetwClockConfig::get_tickrate
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "tickrate",
            PROPERTY_HINT_NONE,
            "suffix:frames"
        ),
        "set_tickrate",
        "get_tickrate"
    );

    ClassDB::bind_method(
        D_METHOD("set_max_ticks_per_frame", "max_ticks_per_frame"),
        &NetwClockConfig::set_max_ticks_per_frame
    );
    ClassDB::bind_method(
        D_METHOD("get_max_ticks_per_frame"),
        &NetwClockConfig::get_max_ticks_per_frame
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "max_ticks_per_frame",
            PROPERTY_HINT_NONE,
            "suffix:ticks"
        ),
        "set_max_ticks_per_frame",
        "get_max_ticks_per_frame"
    );

    ClassDB::bind_method(
        D_METHOD("set_stall_threshold", "stall_threshold"),
        &NetwClockConfig::set_stall_threshold
    );
    ClassDB::bind_method(
        D_METHOD("get_stall_threshold"),
        &NetwClockConfig::get_stall_threshold
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "stall_threshold",
            PROPERTY_HINT_NONE,
            "suffix:s"
        ),
        "set_stall_threshold",
        "get_stall_threshold"
    );

    ClassDB::bind_method(
        D_METHOD("set_use_physics_interpolation", "use_physics_interpolation"),
        &NetwClockConfig::set_use_physics_interpolation
    );
    ClassDB::bind_method(
        D_METHOD("get_use_physics_interpolation"),
        &NetwClockConfig::get_use_physics_interpolation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "use_physics_interpolation"),
        "set_use_physics_interpolation",
        "get_use_physics_interpolation"
    );

    ClassDB::bind_method(
        D_METHOD("set_sync_mode", "sync_mode"),
        &NetwClockConfig::set_sync_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_sync_mode"),
        &NetwClockConfig::get_sync_mode
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

    ClassDB::bind_method(
        D_METHOD("set_panic_snap_threshold", "panic_snap_threshold"),
        &NetwClockConfig::set_panic_snap_threshold
    );
    ClassDB::bind_method(
        D_METHOD("get_panic_snap_threshold"),
        &NetwClockConfig::get_panic_snap_threshold
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "panic_snap_threshold",
            PROPERTY_HINT_NONE,
            "suffix:ticks"
        ),
        "set_panic_snap_threshold",
        "get_panic_snap_threshold"
    );

    ClassDB::bind_method(
        D_METHOD("set_stretch_nudge_factor", "stretch_nudge_factor"),
        &NetwClockConfig::set_stretch_nudge_factor
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_nudge_factor"),
        &NetwClockConfig::get_stretch_nudge_factor
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "stretch_nudge_factor",
            PROPERTY_HINT_RANGE,
            "0.01,0.5"
        ),
        "set_stretch_nudge_factor",
        "get_stretch_nudge_factor"
    );

    ClassDB::bind_method(
        D_METHOD("set_ping_interval", "ping_interval"),
        &NetwClockConfig::set_ping_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_ping_interval"),
        &NetwClockConfig::get_ping_interval
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "ping_interval",
            PROPERTY_HINT_NONE,
            "suffix:s"
        ),
        "set_ping_interval",
        "get_ping_interval"
    );

    ClassDB::bind_method(
        D_METHOD("set_display_offset", "display_offset"),
        &NetwClockConfig::set_display_offset
    );
    ClassDB::bind_method(
        D_METHOD("get_display_offset"),
        &NetwClockConfig::get_display_offset
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "display_offset",
            PROPERTY_HINT_NONE,
            "suffix:ticks"
        ),
        "set_display_offset",
        "get_display_offset"
    );

    ClassDB::bind_method(
        D_METHOD("set_jitter_multiplier", "jitter_multiplier"),
        &NetwClockConfig::set_jitter_multiplier
    );
    ClassDB::bind_method(
        D_METHOD("get_jitter_multiplier"),
        &NetwClockConfig::get_jitter_multiplier
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "jitter_multiplier"),
        "set_jitter_multiplier",
        "get_jitter_multiplier"
    );

    ClassDB::bind_method(
        D_METHOD("set_jitter_window", "jitter_window"),
        &NetwClockConfig::set_jitter_window
    );
    ClassDB::bind_method(
        D_METHOD("get_jitter_window"),
        &NetwClockConfig::get_jitter_window
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "jitter_window",
            PROPERTY_HINT_NONE,
            "suffix:samples"
        ),
        "set_jitter_window",
        "get_jitter_window"
    );

    ClassDB::bind_method(
        D_METHOD(
            "set_jitter_stability_threshold",
            "jitter_stability_threshold"
        ),
        &NetwClockConfig::set_jitter_stability_threshold
    );
    ClassDB::bind_method(
        D_METHOD("get_jitter_stability_threshold"),
        &NetwClockConfig::get_jitter_stability_threshold
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "jitter_stability_threshold",
            PROPERTY_HINT_NONE,
            "suffix:s"
        ),
        "set_jitter_stability_threshold",
        "get_jitter_stability_threshold"
    );

    ClassDB::bind_method(
        D_METHOD("set_enable_drift_logging", "enable_drift_logging"),
        &NetwClockConfig::set_enable_drift_logging
    );
    ClassDB::bind_method(
        D_METHOD("get_enable_drift_logging"),
        &NetwClockConfig::get_enable_drift_logging
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "enable_drift_logging"),
        "set_enable_drift_logging",
        "get_enable_drift_logging"
    );

    ClassDB::bind_method(
        D_METHOD("set_tickrate_mismatch_action", "tickrate_mismatch_action"),
        &NetwClockConfig::set_tickrate_mismatch_action
    );
    ClassDB::bind_method(
        D_METHOD("get_tickrate_mismatch_action"),
        &NetwClockConfig::get_tickrate_mismatch_action
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "tickrate_mismatch_action",
            PROPERTY_HINT_ENUM,
            "Warn,Disconnect,Signal"
        ),
        "set_tickrate_mismatch_action",
        "get_tickrate_mismatch_action"
    );

    ClassDB::bind_method(
        D_METHOD("tickrate", "tickrate"),
        &NetwClockConfig::tickrate
    );
    ClassDB::bind_method(
        D_METHOD("display_offset", "display_offset"),
        &NetwClockConfig::display_offset
    );
    ClassDB::bind_method(
        D_METHOD("physics_interpolation", "use_physics_interpolation"),
        &NetwClockConfig::physics_interpolation
    );
    ClassDB::bind_method(
        D_METHOD("sync_mode", "sync_mode"),
        &NetwClockConfig::sync_mode
    );
}

Ref<NetwClockConfig> NetwClockConfig::tickrate(int64_t p_value) {
    set_tickrate(p_value);
    return Ref<NetwClockConfig>(this);
}

Ref<NetwClockConfig> NetwClockConfig::display_offset(int64_t p_value) {
    set_display_offset(p_value);
    return Ref<NetwClockConfig>(this);
}

Ref<NetwClockConfig> NetwClockConfig::physics_interpolation(bool p_value) {
    set_use_physics_interpolation(p_value);
    return Ref<NetwClockConfig>(this);
}

Ref<NetwClockConfig> NetwClockConfig::sync_mode(int64_t p_value) {
    set_sync_mode(p_value);
    return Ref<NetwClockConfig>(this);
}

void NetwClockConfig::copy_values_from(const NetwClockConfig &p_source) {
    values = p_source.values;
}

} // namespace netw
