#include "netw/display/history.hpp"

#include <algorithm>
#include <cmath>

#include "godot/math.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/project.hpp"

namespace netw::display {

using namespace godot;

namespace {

constexpr int64_t DEFAULT_CAPACITY = 16;

constexpr double SETTLED = 0.001;
constexpr double NEGLIGIBLE = 0.0001;

constexpr double TURN = 6.2831853071795864769252867666;

bool is_spatial(int type) {
    return type == Variant::VECTOR2 || type == Variant::VECTOR2I
        || type == Variant::VECTOR3 || type == Variant::VECTOR3I;
}

double spatial_distance(const Variant &from, const Variant &to) {
    if (from.get_type() == Variant::VECTOR2
        || from.get_type() == Variant::VECTOR2I) {
        return Vector2(from).distance_to(Vector2(to));
    }
    return Vector3(from).distance_to(Vector3(to));
}

} // namespace

double angle_difference(double p_from, double p_to) {
    const double difference = std::fmod(p_to - p_from, TURN);
    return std::fmod(2.0 * difference, TURN) - difference;
}

int History::pass_verdict(
    const Ref<NetwInterpolate> &p_spec,
    bool p_forecast
) const {
    if (sleeping) {
        return PASS_SKIP_SLEEPING;
    }
    if (is_empty()) {
        return PASS_SKIP_EMPTY;
    }
    const bool holds = p_spec.is_valid()
        && p_spec->get_forecast_tail() == NetwInterpolate::TAIL_HOLD;
    return p_forecast && !holds ? PASS_SAMPLE_PROJECT : PASS_SAMPLE;
}

History::History() {
    buffer = NetwRingBuffer::create(DEFAULT_CAPACITY);
}

void History::record(int64_t tick, const Variant &value, bool authoring_tick) {
    NETW_ZONE_SYS(profile::SUBSYSTEM_INTERPOLATION);
    const int domain = authoring_tick ? 1 : 0;
    if (tick_domain == -1) {
        tick_domain = domain;
    } else {
        NETW_ERR_COND(
            tick_domain != domain,
            sys::INTERPOLATION,
            "A display channel cannot mix authoring and receive ticks."
        );
    }

    const int type = value.get_type();
    if (value_type == Variant::NIL) {
        value_type = type;
    } else {
        NETW_ERR_COND(
            value_type != type,
            sys::INTERPOLATION,
            "A display channel cannot change its value type."
        );
    }

    if (has_recorded && value == last_recorded) {
        return;
    }
    buffer->record(tick, value);
    last_recorded = value;
    has_recorded = true;
    sleeping = false;
}

void History::clear() {
    buffer->clear();
    tick_domain = -1;
    has_recorded = false;
    sleeping = false;
}

bool History::is_empty() const {
    return buffer->is_empty();
}

int64_t History::newest_tick() const {
    return buffer->newest_tick();
}

bool History::has_tick_after(int64_t tick) const {
    return buffer->has_tick_after(tick);
}

Vector2i History::bracketing_ticks(int64_t tick) const {
    return buffer->bracketing_ticks(tick);
}

Variant History::get_at(int64_t tick) const {
    return buffer->get_at(tick);
}

Variant History::sample(
    int64_t dt,
    double factor,
    const Variant &last_written,
    int64_t expected_interval_ticks,
    bool forecast,
    int64_t max_forecast_ticks,
    double ticktime,
    const Variant &explicit_velocity,
    bool has_explicit_velocity
) {
    NETW_ZONE_SYS(profile::SUBSYSTEM_INTERPOLATION);
    projected = false;
    snap_taken = false;

    const Vector2i bracket = buffer->bracketing_ticks(dt);
    const int64_t previous_tick = bracket.x;
    const int64_t next_tick = bracket.y;
    if (previous_tick == -1) {
        return last_written;
    }

    if (next_tick == -1) {
        const Variant result = buffer->get_at(previous_tick);
        if (forecast && project::supports(result.get_type())) {
            const Variant velocity = has_explicit_velocity
                ? explicit_velocity
                : finite_velocity(previous_tick, ticktime);
            if (velocity.get_type() != Variant::NIL
                && !velocity_negligible(velocity)) {
                const double age = std::clamp(
                    (double(dt) + factor) - double(previous_tick),
                    0.0,
                    double(max_forecast_ticks)
                );
                projected = true;
                project_age = age;
                return project::forward(result, velocity, age * ticktime);
            }
        }
        if (!buffer->has_tick_after(previous_tick)
            && is_close(last_written, result)) {
            sleeping = true;
        }
        return result;
    }

    const Variant previous = buffer->get_at(previous_tick);
    const Variant next = buffer->get_at(next_tick);
    if (snap_distance > 0.0 && exceeds(previous, next, snap_distance)) {
        snap_taken = true;
        return next;
    }
    return lerp_bracketed(
        previous,
        next,
        previous_tick,
        next_tick,
        dt,
        factor,
        expected_interval_ticks
    );
}

Variant History::smooth_toward(
    const Variant &last_written,
    const Variant &result,
    double weight
) {
    if (weight >= 1.0) {
        return result;
    }
    if (snap_distance > 0.0 && exceeds(last_written, result, snap_distance)) {
        snap_taken = true;
        return result;
    }
    return interpolate(last_written, result, weight);
}

Variant History::finite_velocity(int64_t newest, double ticktime) const {
    if (ticktime <= 0.0) {
        return Variant();
    }
    const int64_t older = buffer->bracketing_ticks(newest - 1).x;
    if (older == -1 || older == newest) {
        return Variant();
    }
    const double span = double(newest - older) * ticktime;
    if (span <= 0.0) {
        return Variant();
    }

    const Variant recent = buffer->get_at(newest);
    const Variant earlier = buffer->get_at(older);
    switch (recent.get_type()) {
        case Variant::FLOAT: {
            const double difference = mode == NetwInterpolate::MODE_ANGLE
                ? angle_difference(double(earlier), double(recent))
                : double(recent) - double(earlier);
            return difference / span;
        }
        case Variant::VECTOR2:
            return (Vector2(recent) - Vector2(earlier)) / real_t(span);
        case Variant::VECTOR3:
            return (Vector3(recent) - Vector3(earlier)) / real_t(span);
        case Variant::QUATERNION: {
            const Quaternion turn
                = Quaternion(recent) * Quaternion(earlier).inverse();
            const double angle
                = 2.0 * std::acos(std::clamp(double(turn.w), -1.0, 1.0));
            if (angle < NEGLIGIBLE) {
                return Vector3();
            }
            const Vector3 axis = Vector3(turn.x, turn.y, turn.z).normalized();
            return axis * real_t(angle / span);
        }
        default:
            return Variant();
    }
}

bool History::velocity_negligible(const Variant &velocity) const {
    switch (velocity.get_type()) {
        case Variant::FLOAT:
            return std::fabs(double(velocity)) < NEGLIGIBLE;
        case Variant::VECTOR2:
            return Vector2(velocity).length() < NEGLIGIBLE;
        case Variant::VECTOR3:
            return Vector3(velocity).length() < NEGLIGIBLE;
        default:
            return false;
    }
}

Variant History::lerp_bracketed(
    const Variant &previous,
    const Variant &next,
    int64_t previous_tick,
    int64_t next_tick,
    int64_t dt,
    double factor,
    int64_t expected_interval_ticks
) const {
    const int64_t gap = next_tick - previous_tick;
    if (gap > expected_interval_ticks * 2) {
        const int64_t start_tick = next_tick - expected_interval_ticks;
        if (dt < start_tick) {
            return previous;
        }
        const double weight = std::clamp(
            (double(dt - start_tick) + factor)
                / double(expected_interval_ticks),
            0.0,
            1.0
        );
        return interpolate(previous, next, weight);
    }
    const double weight = std::clamp(
        (double(dt - previous_tick) + factor) / double(gap),
        0.0,
        1.0
    );
    return interpolate(previous, next, weight);
}

Variant History::interpolate(
    const Variant &from,
    const Variant &to,
    double weight
) const {
    if (mode == NetwInterpolate::MODE_ANGLE) {
        return Math::lerp_angle(double(from), double(to), weight);
    }
    switch (to.get_type()) {
        case Variant::FLOAT:
            return Math::lerp(double(from), double(to), weight);
        case Variant::VECTOR2:
            return Vector2(from).lerp(Vector2(to), real_t(weight));
        case Variant::VECTOR3:
            return Vector3(from).lerp(Vector3(to), real_t(weight));
        case Variant::QUATERNION:
            return Quaternion(from).slerp(Quaternion(to), real_t(weight));
        case Variant::COLOR:
            return Color(from).lerp(Color(to), real_t(weight));
        default:
            return to;
    }
}

bool History::exceeds(
    const Variant &from,
    const Variant &to,
    double distance
) const {
    if (from.get_type() != to.get_type()) {
        return true;
    }
    const int type = from.get_type();
    if (is_spatial(type)) {
        return spatial_distance(from, to) > distance;
    }
    if (type == Variant::QUATERNION) {
        return std::fabs(Quaternion(from).angle_to(Quaternion(to))) > distance;
    }
    if (type == Variant::FLOAT || type == Variant::INT) {
        const double difference = mode == NetwInterpolate::MODE_ANGLE
            ? angle_difference(double(from), double(to))
            : double(from) - double(to);
        return std::fabs(difference) > distance;
    }
    return false;
}

bool History::is_close(const Variant &from, const Variant &to) const {
    if (from.get_type() != to.get_type()) {
        return false;
    }
    const int type = from.get_type();
    if (is_spatial(type)) {
        if (type == Variant::VECTOR2 || type == Variant::VECTOR2I) {
            return Vector2(from).is_equal_approx(Vector2(to));
        }
        return Vector3(from).is_equal_approx(Vector3(to));
    }
    if (type == Variant::QUATERNION) {
        return std::fabs(Quaternion(from).angle_to(Quaternion(to))) < SETTLED;
    }
    if (type == Variant::FLOAT || type == Variant::INT) {
        const double difference = mode == NetwInterpolate::MODE_ANGLE
            ? angle_difference(double(from), double(to))
            : double(from) - double(to);
        return std::fabs(difference) < SETTLED;
    }
    return true;
}

} // namespace netw::display
