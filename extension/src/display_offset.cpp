#include "netw/display_offset.hpp"

#include <cmath>

#include "netw/display_history.hpp"
#include "netw/api/interpolate.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr double NEGLIGIBLE = 0.0001;

Variant scaled(const Variant &p_delta, double p_factor) {
    switch (p_delta.get_type()) {
        case Variant::FLOAT:
            return double(p_delta) * p_factor;
        case Variant::VECTOR2:
            return Vector2(p_delta) * real_t(p_factor);
        case Variant::VECTOR3:
            return Vector3(p_delta) * real_t(p_factor);
        case Variant::QUATERNION:
            return Quaternion().slerp(Quaternion(p_delta), real_t(p_factor));
        default:
            return Variant();
    }
}

Variant clamped(const Variant &p_delta, double p_limit) {
    switch (p_delta.get_type()) {
        case Variant::FLOAT:
            return CLAMP(double(p_delta), -p_limit, p_limit);
        case Variant::VECTOR2:
            return Vector2(p_delta).limit_length(real_t(p_limit));
        case Variant::VECTOR3:
            return Vector3(p_delta).limit_length(real_t(p_limit));
        case Variant::QUATERNION: {
            const Quaternion rotation = Quaternion(p_delta);
            const double angle = Quaternion().angle_to(rotation);
            if (angle <= p_limit || angle <= 0.0) {
                return rotation;
            }
            return Quaternion().slerp(rotation, real_t(p_limit / angle));
        }
        default:
            return Variant();
    }
}

Variant composed(const Variant &p_value, const Variant &p_delta) {
    switch (p_delta.get_type()) {
        case Variant::FLOAT:
            return double(p_value) + double(p_delta);
        case Variant::VECTOR2:
            return Vector2(p_value) + Vector2(p_delta);
        case Variant::VECTOR3:
            return Vector3(p_value) + Vector3(p_delta);
        case Variant::QUATERNION:
            return (Quaternion(p_delta) * Quaternion(p_value)).normalized();
        default:
            return p_value;
    }
}

bool spent(const Variant &p_delta) {
    switch (p_delta.get_type()) {
        case Variant::FLOAT:
            return std::abs(double(p_delta)) < NEGLIGIBLE;
        case Variant::VECTOR2:
            return Vector2(p_delta).length() < NEGLIGIBLE;
        case Variant::VECTOR3:
            return Vector3(p_delta).length() < NEGLIGIBLE;
        case Variant::QUATERNION:
            return Quaternion().angle_to(Quaternion(p_delta)) < NEGLIGIBLE;
        default:
            return true;
    }
}

Variant residual_of(
    const Variant &p_displayed,
    const Variant &p_target,
    int64_t p_mode
) {
    if (p_displayed.get_type() != p_target.get_type()) {
        return Variant();
    }
    switch (p_target.get_type()) {
        case Variant::FLOAT:
            return p_mode == NetwInterpolate::MODE_ANGLE
                ? angle_difference(double(p_target), double(p_displayed))
                : double(p_displayed) - double(p_target);
        case Variant::VECTOR2:
            return Vector2(p_displayed) - Vector2(p_target);
        case Variant::VECTOR3:
            return Vector3(p_displayed) - Vector3(p_target);
        case Variant::QUATERNION:
            return Quaternion(p_displayed) * Quaternion(p_target).inverse();
        default:
            return Variant();
    }
}

} // namespace

void DisplayOffset::clear() {
    residual = Variant();
    armed = false;
}

bool DisplayOffset::is_held() const {
    return residual.get_type() != Variant::NIL;
}

void DisplayOffset::absorb(const Variant &p_recovery, double p_limit) {
    Variant absorbed = scaled(p_recovery, -1.0);
    if (is_held() && absorbed.get_type() != Variant::NIL) {
        absorbed = composed(residual, absorbed);
    }
    residual = clamped(absorbed, p_limit);
    armed = false;
}

Variant DisplayOffset::apply(
    const Variant &p_target,
    double p_glide,
    double p_limit,
    const Variant &p_displayed,
    int64_t p_mode
) {
    if (armed) {
        armed = false;
        residual = clamped(residual_of(p_displayed, p_target, p_mode), p_limit);
    }
    if (!is_held()) {
        return p_target;
    }
    residual = scaled(residual, p_glide);
    if (!is_held() || spent(residual)) {
        residual = Variant();
        return p_target;
    }
    return composed(p_target, residual);
}

} // namespace netw
