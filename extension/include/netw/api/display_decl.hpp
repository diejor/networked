#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwDisplayDecl : public godot::RefCounted {
    GDCLASS(NetwDisplayDecl, godot::RefCounted)

public:
    enum Param {
        PARAM_ROLE = 0,
        PARAM_PREDICTED_MODE = 1,
        PARAM_PREDICTED_SMOOTH_TIME = 2,
        PARAM_CHASE_GLIDE_TIME = 3,
        PARAM_TIMELINE_MODE = 4,
        PARAM_MAX_FORECAST_TICKS = 5,
        PARAM_SMART_DILATION = 6,
        PARAM_MAX_EXTRA_DILATION = 7,
        PARAM_LAG_ADAPT_RATE = 8,
        PARAM_STARVATION_GROWTH = 9,
        PARAM_FLOOR_SMOOTHING = 10,
        PARAM_STARVATION_GRACE_FRAMES = 11,
        PARAM_TRACE_INTERVAL = 12,
        PARAM_VISUAL_ROOT = 13,
        PARAM_MAX = 14,
    };

    enum Dirt {
        DIRT_NONE = 0,
        DIRT_ROLE = 1,
        DIRT_RUNTIME = 2,
    };

    enum Role {
        ROLE_AUTO = 0,
        ROLE_REMOTE = 1,
        ROLE_PREDICTED = 2,
        ROLE_DISABLED = 3,
        ROLE_AUTHORITY = 4,
        ROLE_MAX = 5,
    };

    enum PredictedMode {
        PREDICTED_CHASE = 0,
        PREDICTED_BRACKETED = 1,
        PREDICTED_MODE_MAX = 2,
    };

    enum Pump {
        PUMP_UNRESOLVED = -1,
        PUMP_DISABLED = 0,
        PUMP_REMOTE = 1,
        PUMP_BRACKETED = 2,
        PUMP_CHASE = 3,
    };

    enum TimelineMode {
        TIMELINE_BUFFERED = 0,
        TIMELINE_FORECAST = 1,
        TIMELINE_MODE_MAX = 2,
    };

private:
    godot::NodePath visual_root;
    int32_t display_role = ROLE_AUTO;
    int32_t predicted_mode = PREDICTED_CHASE;
    int32_t timeline_mode = TIMELINE_BUFFERED;
    int32_t max_forecast_ticks = 6;
    int32_t starvation_grace_frames = 3;
    int32_t trace_interval = 0;
    double predicted_smooth_time = 0.0;
    double chase_glide_time = 0.15;
    double max_extra_dilation = 0.0;
    double lag_adapt_rate = 0.05;
    double starvation_growth = 0.95;
    double floor_smoothing = 0.05;
    bool enable_smart_dilation = true;

protected:
    static void _bind_methods();

public:
    Dirt set_param(int param, const godot::Variant &value);

    godot::Variant get_param(int param) const;

    int pump_for(int role) const;

    static bool pump_is_predicted(int pump);
    static bool pump_clears_history(int previous, int next);
    static bool pump_arms_offsets(int previous, int next);

    godot::NodePath get_visual_root() const { return visual_root; }
    int get_display_role() const { return display_role; }
    int get_predicted_mode() const { return predicted_mode; }
    int get_timeline_mode() const { return timeline_mode; }
    int get_max_forecast_ticks() const { return max_forecast_ticks; }
    int get_starvation_grace_frames() const { return starvation_grace_frames; }
    int get_trace_interval() const { return trace_interval; }
    double get_predicted_smooth_time() const { return predicted_smooth_time; }
    double get_chase_glide_time() const { return chase_glide_time; }
    double get_max_extra_dilation() const { return max_extra_dilation; }
    double get_lag_adapt_rate() const { return lag_adapt_rate; }
    double get_starvation_growth() const { return starvation_growth; }
    double get_floor_smoothing() const { return floor_smoothing; }
    bool get_enable_smart_dilation() const { return enable_smart_dilation; }
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwDisplayDecl::Dirt);
VARIANT_ENUM_CAST(netw::NetwDisplayDecl::Pump);
