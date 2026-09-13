#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::display {

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

struct Decl {
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

    Dirt set_param(int param, const godot::Variant &value);
    godot::Variant get_param(int param) const;
    int pump_for(int role) const;
};

bool pump_is_predicted(int pump);
bool pump_clears_history(int previous, int next);
bool pump_arms_offsets(int previous, int next);

} // namespace netw::display
