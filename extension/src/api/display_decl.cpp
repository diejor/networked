#include "netw/api/display_decl.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

bool ordinal_names_one(int value, int limit, const char *what) {
    NETW_ERR_COND_V(
        value < 0 || value >= limit,
        false,
        sys::INTERPOLATION,
        "NetwDisplayDecl.set_param: %s ordinal %d names nothing.",
        what,
        value
    );
    return true;
}

} // namespace

NetwDisplayDecl::Dirt NetwDisplayDecl::set_param(
    int param,
    const Variant &value
) {
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
                "NetwDisplayDecl.set_param: a visual root is a path, not a %s.",
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
        "NetwDisplayDecl.set_param: %d names no display setting.",
        param
    );
}

Variant NetwDisplayDecl::get_param(int param) const {
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
        "NetwDisplayDecl.get_param: %d names no display setting.",
        param
    );
}

#define NETW_DISPLAY_FIELD(m_type, m_name)                                     \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwDisplayDecl::get_##m_name                                         \
    );                                                                         \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name), "", "get_" #m_name)

void NetwDisplayDecl::_bind_methods() {
    BIND_ENUM_CONSTANT(DIRT_NONE);
    BIND_ENUM_CONSTANT(DIRT_ROLE);
    BIND_ENUM_CONSTANT(DIRT_RUNTIME);

    BIND_ENUM_CONSTANT(PUMP_UNRESOLVED);
    BIND_ENUM_CONSTANT(PUMP_DISABLED);
    BIND_ENUM_CONSTANT(PUMP_REMOTE);
    BIND_ENUM_CONSTANT(PUMP_BRACKETED);
    BIND_ENUM_CONSTANT(PUMP_CHASE);

    ClassDB::bind_method(
        D_METHOD("set_param", "param", "value"),
        &NetwDisplayDecl::set_param
    );
    ClassDB::bind_method(
        D_METHOD("get_param", "param"),
        &NetwDisplayDecl::get_param
    );
    ClassDB::bind_method(
        D_METHOD("pump_for", "role"),
        &NetwDisplayDecl::pump_for
    );
    ClassDB::bind_static_method(
        "NetwDisplayDecl",
        D_METHOD("pump_is_predicted", "pump"),
        &NetwDisplayDecl::pump_is_predicted
    );
    ClassDB::bind_static_method(
        "NetwDisplayDecl",
        D_METHOD("pump_clears_history", "previous", "next"),
        &NetwDisplayDecl::pump_clears_history
    );
    ClassDB::bind_static_method(
        "NetwDisplayDecl",
        D_METHOD("pump_arms_offsets", "previous", "next"),
        &NetwDisplayDecl::pump_arms_offsets
    );

    NETW_DISPLAY_FIELD(Variant::NODE_PATH, visual_root);
    NETW_DISPLAY_FIELD(Variant::INT, display_role);
    NETW_DISPLAY_FIELD(Variant::INT, predicted_mode);
    NETW_DISPLAY_FIELD(Variant::INT, timeline_mode);
    NETW_DISPLAY_FIELD(Variant::INT, max_forecast_ticks);
    NETW_DISPLAY_FIELD(Variant::INT, starvation_grace_frames);
    NETW_DISPLAY_FIELD(Variant::INT, trace_interval);
    NETW_DISPLAY_FIELD(Variant::FLOAT, predicted_smooth_time);
    NETW_DISPLAY_FIELD(Variant::FLOAT, chase_glide_time);
    NETW_DISPLAY_FIELD(Variant::FLOAT, max_extra_dilation);
    NETW_DISPLAY_FIELD(Variant::FLOAT, lag_adapt_rate);
    NETW_DISPLAY_FIELD(Variant::FLOAT, starvation_growth);
    NETW_DISPLAY_FIELD(Variant::FLOAT, floor_smoothing);
    NETW_DISPLAY_FIELD(Variant::BOOL, enable_smart_dilation);
}

bool NetwDisplayDecl::pump_is_predicted(int pump) {
    return pump == PUMP_CHASE || pump == PUMP_BRACKETED;
}

bool NetwDisplayDecl::pump_clears_history(int previous, int next) {
    if (previous == PUMP_CHASE && next == PUMP_REMOTE) {
        return false;
    }
    return pump_is_predicted(previous) || pump_is_predicted(next);
}

bool NetwDisplayDecl::pump_arms_offsets(int previous, int next) {
    return previous != PUMP_UNRESOLVED && previous != PUMP_DISABLED
        && next != PUMP_DISABLED;
}

int NetwDisplayDecl::pump_for(int role) const {
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

} // namespace netw
