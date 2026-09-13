#include "netw/display/decl.hpp"

#include "netw/log.hpp"

using namespace godot;

namespace netw::display {

namespace {

bool ordinal_names_one(int value, int limit, const char *what) {
    NETW_ERR_COND_V(
        value < 0 || value >= limit,
        false,
        sys::INTERPOLATION,
        "display decl set_param: %s ordinal %d names nothing.",
        what,
        value
    );
    return true;
}

} // namespace

Dirt Decl::set_param(int param, const Variant &value) {
    switch (param) {
        case PARAM_ROLE: {
            const int ordinal = int(value);
            if (!ordinal_names_one(ordinal, ROLE_MAX, "role")) {
                return DIRT_NONE;
            }
            display_role = ordinal;
            return DIRT_ROLE;
        }
        case PARAM_PREDICTED_MODE: {
            const int ordinal = int(value);
            if (!ordinal_names_one(
                    ordinal,
                    PREDICTED_MODE_MAX,
                    "predicted mode"
                )) {
                return DIRT_NONE;
            }
            predicted_mode = ordinal;
            return DIRT_ROLE;
        }
        case PARAM_TIMELINE_MODE: {
            const int ordinal = int(value);
            if (!ordinal_names_one(
                    ordinal,
                    TIMELINE_MODE_MAX,
                    "timeline mode"
                )) {
                return DIRT_NONE;
            }
            timeline_mode = ordinal;
            return DIRT_NONE;
        }
        case PARAM_VISUAL_ROOT: {
            const Variant::Type type = value.get_type();
            NETW_ERR_COND_V(
                type != Variant::NODE_PATH && type != Variant::STRING
                    && type != Variant::STRING_NAME,
                DIRT_NONE,
                sys::INTERPOLATION,
                "display decl set_param: a visual root is a path, not a %s.",
                Variant::get_type_name(type)
            );
            visual_root = value.operator NodePath();
            return DIRT_RUNTIME;
        }
        case PARAM_PREDICTED_SMOOTH_TIME:
            predicted_smooth_time = double(value);
            return DIRT_RUNTIME;
        case PARAM_CHASE_GLIDE_TIME:
            chase_glide_time = double(value);
            return DIRT_NONE;
        case PARAM_MAX_FORECAST_TICKS:
            max_forecast_ticks = int(value);
            return DIRT_NONE;
        case PARAM_SMART_DILATION:
            enable_smart_dilation = bool(value);
            return DIRT_NONE;
        case PARAM_MAX_EXTRA_DILATION:
            max_extra_dilation = double(value);
            return DIRT_NONE;
        case PARAM_LAG_ADAPT_RATE:
            lag_adapt_rate = double(value);
            return DIRT_NONE;
        case PARAM_STARVATION_GROWTH:
            starvation_growth = double(value);
            return DIRT_NONE;
        case PARAM_FLOOR_SMOOTHING:
            floor_smoothing = double(value);
            return DIRT_NONE;
        case PARAM_STARVATION_GRACE_FRAMES:
            starvation_grace_frames = int(value);
            return DIRT_NONE;
        case PARAM_TRACE_INTERVAL:
            trace_interval = int(value);
            return DIRT_NONE;
        default:
            break;
    }
    NETW_ERR_V(
        DIRT_NONE,
        sys::INTERPOLATION,
        "display decl set_param: %d names no display setting.",
        param
    );
}

Variant Decl::get_param(int param) const {
    switch (param) {
        case PARAM_ROLE:
            return display_role;
        case PARAM_PREDICTED_MODE:
            return predicted_mode;
        case PARAM_PREDICTED_SMOOTH_TIME:
            return predicted_smooth_time;
        case PARAM_CHASE_GLIDE_TIME:
            return chase_glide_time;
        case PARAM_TIMELINE_MODE:
            return timeline_mode;
        case PARAM_MAX_FORECAST_TICKS:
            return max_forecast_ticks;
        case PARAM_SMART_DILATION:
            return enable_smart_dilation;
        case PARAM_MAX_EXTRA_DILATION:
            return max_extra_dilation;
        case PARAM_LAG_ADAPT_RATE:
            return lag_adapt_rate;
        case PARAM_STARVATION_GROWTH:
            return starvation_growth;
        case PARAM_FLOOR_SMOOTHING:
            return floor_smoothing;
        case PARAM_STARVATION_GRACE_FRAMES:
            return starvation_grace_frames;
        case PARAM_TRACE_INTERVAL:
            return trace_interval;
        case PARAM_VISUAL_ROOT:
            return visual_root;
        default:
            break;
    }
    NETW_ERR_V(
        Variant(),
        sys::INTERPOLATION,
        "display decl get_param: %d names no display setting.",
        param
    );
}

bool pump_is_predicted(int pump) {
    return pump == PUMP_CHASE || pump == PUMP_BRACKETED;
}

bool pump_clears_history(int previous, int next) {
    if (previous == PUMP_CHASE && next == PUMP_REMOTE) {
        return false;
    }
    return pump_is_predicted(previous) || pump_is_predicted(next);
}

bool pump_arms_offsets(int previous, int next) {
    return previous != PUMP_UNRESOLVED && previous != PUMP_DISABLED
        && next != PUMP_DISABLED;
}

int Decl::pump_for(int role) const {
    switch (role) {
        case ROLE_PREDICTED:
            return predicted_mode == PREDICTED_BRACKETED ? PUMP_BRACKETED
                                                         : PUMP_CHASE;
        case ROLE_AUTHORITY:
            return PUMP_BRACKETED;
        case ROLE_REMOTE:
            return PUMP_REMOTE;
        default:
            return PUMP_DISABLED;
    }
}

} // namespace netw::display
