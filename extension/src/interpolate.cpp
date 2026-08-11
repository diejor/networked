#include "netw/interpolate.hpp"

#include "godot/class_db.hpp"
#include "godot/math.hpp"

namespace netw {

using namespace godot;

void NetwInterpolate::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_mode", "mode"), &NetwInterpolate::set_mode);
    ClassDB::bind_method(D_METHOD("get_mode"), &NetwInterpolate::get_mode);
    ClassDB::bind_method(
        D_METHOD("set_smoothing", "seconds"),
        &NetwInterpolate::set_smoothing
    );
    ClassDB::bind_method(
        D_METHOD("get_smoothing"),
        &NetwInterpolate::get_smoothing
    );
    ClassDB::bind_method(
        D_METHOD("set_snap_distance", "distance"),
        &NetwInterpolate::set_snap_distance
    );
    ClassDB::bind_method(
        D_METHOD("get_snap_distance"),
        &NetwInterpolate::get_snap_distance
    );
    ClassDB::bind_method(
        D_METHOD("set_target", "property"),
        &NetwInterpolate::set_target
    );
    ClassDB::bind_method(D_METHOD("get_target"), &NetwInterpolate::get_target);
    ClassDB::bind_method(
        D_METHOD("set_forecast_tail", "tail"),
        &NetwInterpolate::set_forecast_tail
    );
    ClassDB::bind_method(
        D_METHOD("get_forecast_tail"),
        &NetwInterpolate::get_forecast_tail
    );
    ClassDB::bind_method(
        D_METHOD("set_project_channel", "channel"),
        &NetwInterpolate::set_project_channel
    );
    ClassDB::bind_method(
        D_METHOD("get_project_channel"),
        &NetwInterpolate::get_project_channel
    );

    ClassDB::bind_method(D_METHOD("none"), &NetwInterpolate::none);
    ClassDB::bind_method(D_METHOD("lerp"), &NetwInterpolate::lerp);
    ClassDB::bind_method(D_METHOD("angle"), &NetwInterpolate::angle);
    ClassDB::bind_method(D_METHOD("slerp"), &NetwInterpolate::slerp);
    ClassDB::bind_method(D_METHOD("smooth", "seconds"), &NetwInterpolate::smooth);
    ClassDB::bind_method(
        D_METHOD("snap_at", "distance"),
        &NetwInterpolate::snap_at
    );
    ClassDB::bind_method(D_METHOD("to", "property"), &NetwInterpolate::to);
    ClassDB::bind_method(
        D_METHOD("project_by", "channel"),
        &NetwInterpolate::project_by
    );
    ClassDB::bind_method(D_METHOD("hold"), &NetwInterpolate::hold);
    ClassDB::bind_method(
        D_METHOD("is_same_spec", "other"),
        &NetwInterpolate::is_same_spec
    );
    ClassDB::bind_method(
        D_METHOD("_supports_type", "type"),
        &NetwInterpolate::supports_type
    );

    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "mode",
            PROPERTY_HINT_ENUM,
            "None,Lerp,Angle,Slerp"
        ),
        "set_mode",
        "get_mode"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "smoothing", PROPERTY_HINT_NONE, "suffix:s"),
        "set_smoothing",
        "get_smoothing"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "snap_distance"),
        "set_snap_distance",
        "get_snap_distance"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "target"),
        "set_target",
        "get_target"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "forecast_tail",
            PROPERTY_HINT_ENUM,
            "Auto,Hold"
        ),
        "set_forecast_tail",
        "get_forecast_tail"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "project_channel"),
        "set_project_channel",
        "get_project_channel"
    );

    BIND_ENUM_CONSTANT(MODE_NONE);
    BIND_ENUM_CONSTANT(MODE_LERP);
    BIND_ENUM_CONSTANT(MODE_ANGLE);
    BIND_ENUM_CONSTANT(MODE_SLERP);

    BIND_ENUM_CONSTANT(TAIL_AUTO);
    BIND_ENUM_CONSTANT(TAIL_HOLD);
}

Ref<NetwInterpolate> NetwInterpolate::none() {
    mode = MODE_NONE;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::lerp() {
    mode = MODE_LERP;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::angle() {
    mode = MODE_ANGLE;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::slerp() {
    mode = MODE_SLERP;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::smooth(double seconds) {
    smoothing = seconds;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::snap_at(double distance) {
    snap_distance = distance;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::to(const StringName &property) {
    target = property;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::project_by(const StringName &channel) {
    project_channel = channel;
    forecast_tail = TAIL_AUTO;
    return Ref<NetwInterpolate>(this);
}

Ref<NetwInterpolate> NetwInterpolate::hold() {
    forecast_tail = TAIL_HOLD;
    return Ref<NetwInterpolate>(this);
}

bool NetwInterpolate::is_same_spec(const Ref<NetwInterpolate> &other) const {
    if (other.is_null()) {
        return false;
    }
    return mode == other->get_mode()
        && Math::is_equal_approx(smoothing, other->get_smoothing())
        && Math::is_equal_approx(snap_distance, other->get_snap_distance())
        && target == other->get_target()
        && forecast_tail == other->get_forecast_tail()
        && project_channel == other->get_project_channel();
}

bool NetwInterpolate::supports_type(int type) const {
    return type == Variant::FLOAT || type == Variant::VECTOR2
        || type == Variant::VECTOR3 || type == Variant::QUATERNION
        || type == Variant::COLOR;
}

} // namespace netw
