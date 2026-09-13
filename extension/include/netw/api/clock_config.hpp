#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "netw/api/config_draft.hpp"

namespace netw {

class NetwClockConfig : public godot::Resource {
    GDCLASS(NetwClockConfig, godot::Resource)

    struct Values {
        int64_t tickrate = 30;
        int64_t max_ticks_per_frame = 8;
        double stall_threshold = 1.0;
        bool use_physics_interpolation = true;
        int64_t sync_mode = 1;
        int64_t panic_snap_threshold = 20;
        double stretch_nudge_factor = 0.05;
        double ping_interval = 0.1;
        int64_t display_offset = 2;
        double jitter_multiplier = 2.0;
        int64_t jitter_window = 16;
        double jitter_stability_threshold = 0.05;
        bool enable_drift_logging = false;
        int64_t tickrate_mismatch_action = 0;
    };

    Values values;
    config_draft::Guard guard{"configure_clock"};

protected:
    static void _bind_methods();

public:
    void set_tickrate(int64_t p_value) {
        if (guard.takes_positive_count("tickrate", p_value)) {
            values.tickrate = p_value;
        }
    }
    int64_t get_tickrate() const {
        return values.tickrate;
    }

    void set_max_ticks_per_frame(int64_t p_value) {
        if (guard.takes_positive_count("max_ticks_per_frame", p_value)) {
            values.max_ticks_per_frame = p_value;
        }
    }
    int64_t get_max_ticks_per_frame() const {
        return values.max_ticks_per_frame;
    }

    void set_stall_threshold(double p_value) {
        if (guard.takes_positive("stall_threshold", p_value)) {
            values.stall_threshold = p_value;
        }
    }
    double get_stall_threshold() const {
        return values.stall_threshold;
    }

    void set_use_physics_interpolation(bool p_value) {
        if (guard.takes("use_physics_interpolation")) {
            values.use_physics_interpolation = p_value;
        }
    }
    bool get_use_physics_interpolation() const {
        return values.use_physics_interpolation;
    }

    void set_sync_mode(int64_t p_value) {
        if (guard.takes_enum("sync_mode", p_value, 2)) {
            values.sync_mode = p_value;
        }
    }
    int64_t get_sync_mode() const {
        return values.sync_mode;
    }

    void set_panic_snap_threshold(int64_t p_value) {
        if (guard.takes_count("panic_snap_threshold", p_value)) {
            values.panic_snap_threshold = p_value;
        }
    }
    int64_t get_panic_snap_threshold() const {
        return values.panic_snap_threshold;
    }

    void set_stretch_nudge_factor(double p_value) {
        if (guard.takes_ratio("stretch_nudge_factor", p_value)) {
            values.stretch_nudge_factor = p_value;
        }
    }
    double get_stretch_nudge_factor() const {
        return values.stretch_nudge_factor;
    }

    void set_ping_interval(double p_value) {
        if (guard.takes_positive("ping_interval", p_value)) {
            values.ping_interval = p_value;
        }
    }
    double get_ping_interval() const {
        return values.ping_interval;
    }

    void set_display_offset(int64_t p_value) {
        if (guard.takes_count("display_offset", p_value)) {
            values.display_offset = p_value;
        }
    }
    int64_t get_display_offset() const {
        return values.display_offset;
    }

    void set_jitter_multiplier(double p_value) {
        if (guard.takes_ratio("jitter_multiplier", p_value)) {
            values.jitter_multiplier = p_value;
        }
    }
    double get_jitter_multiplier() const {
        return values.jitter_multiplier;
    }

    void set_jitter_window(int64_t p_value) {
        if (guard.takes_positive_count("jitter_window", p_value)) {
            values.jitter_window = p_value;
        }
    }
    int64_t get_jitter_window() const {
        return values.jitter_window;
    }

    void set_jitter_stability_threshold(double p_value) {
        if (guard.takes_ratio("jitter_stability_threshold", p_value)) {
            values.jitter_stability_threshold = p_value;
        }
    }
    double get_jitter_stability_threshold() const {
        return values.jitter_stability_threshold;
    }

    void set_enable_drift_logging(bool p_value) {
        if (guard.takes("enable_drift_logging")) {
            values.enable_drift_logging = p_value;
        }
    }
    bool get_enable_drift_logging() const {
        return values.enable_drift_logging;
    }

    void set_tickrate_mismatch_action(int64_t p_value) {
        if (guard.takes_enum("tickrate_mismatch_action", p_value, 3)) {
            values.tickrate_mismatch_action = p_value;
        }
    }
    int64_t get_tickrate_mismatch_action() const {
        return values.tickrate_mismatch_action;
    }

    godot::Ref<NetwClockConfig> tickrate(int64_t p_value);
    godot::Ref<NetwClockConfig> display_offset(int64_t p_value);
    godot::Ref<NetwClockConfig> physics_interpolation(bool p_value);
    godot::Ref<NetwClockConfig> sync_mode(int64_t p_value);

    void seal(const godot::String &p_scope) {
        guard.seal(p_scope);
    }
    void copy_values_from(const NetwClockConfig &p_source);
};

} // namespace netw
