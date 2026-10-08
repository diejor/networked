#include "netw/display/history.hpp"

#include <algorithm>
#include <cmath>

#include "godot/math.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/project.hpp"
#include "netw/store/validate.hpp"
#include "netw/store/world.hpp"

namespace netw::display {

using namespace godot;

namespace {

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

#if defined(NETW_TESTS)
struct SampleMeter {
    bool metered = false;
    int64_t opened = 0;

    explicit SampleMeter(bool p_metered)
        : metered(p_metered), opened(store::allocations_seen()) {
    }

    ~SampleMeter() {
        if (metered) {
            sample_work().samples += 1;
            sample_work().allocations += store::allocations_seen() - opened;
        }
    }
};
#endif

} // namespace

#if defined(NETW_TESTS)
SampleWork &sample_work() {
    static SampleWork work;
    return work;
}
#endif

double angle_difference(double p_from, double p_to) {
    const double difference = std::fmod(p_to - p_from, TURN);
    return std::fmod(2.0 * difference, TURN) - difference;
}

History::History() : ring(TickRing::DEFAULT_CAPACITY) {
}

History::Rows History::rows() const {
    Rows out;
    if (world == nullptr) {
        out.count = ring.size();
        return out;
    }
    const store::RowAddress at = world->row_of(owner, lane);
    if (!at.valid()) {
        return out;
    }
    const store::Table &table = world->table(at.table);
    if (column >= table.columns.size() || at.row >= table.row_count()) {
        return out;
    }
    out.table = &table;
    out.row = at.row;
    if (floor_pending) {
        return out;
    }
    int64_t visible = int64_t(table.history_appends[at.row] - floor_append);
    if (visible < 1 && table.sources[at.row] == uint8_t(store::Source::LOCAL)) {
        visible = 1;
    }
    out.count = std::min(int64_t(table.history_count(at.row)), visible);
    return out;
}

int64_t History::received_in(const Rows &p_rows) const {
    if (p_rows.table == nullptr
        || p_rows.table->sources[p_rows.row]
            != uint8_t(store::Source::REMOTE)) {
        return -1;
    }
    return p_rows.table->received_ticks[p_rows.row];
}

bool History::finished_in(const Rows &p_rows, int64_t p_tick) const {
    const int64_t received = received_in(p_rows);
    if (received < 0 || received <= p_rows.table->history_tick(p_rows.row, 0)) {
        return false;
    }
    const int64_t heartbeat = p_rows.table->heartbeat;
    return heartbeat <= 0 || p_tick - received <= heartbeat;
}

int64_t History::received_tick() const {
    return received_in(rows());
}

bool History::stream_finished(int64_t p_tick) const {
    return finished_in(rows(), p_tick);
}

int64_t History::tick_in(const Rows &p_rows, int64_t p_age) const {
    if (p_age < 0 || p_age >= p_rows.count) {
        return -1;
    }
    return p_rows.table != nullptr
        ? p_rows.table->history_tick(p_rows.row, uint32_t(p_age))
        : ring.tick_of_age(p_age);
}

Variant History::value_in(const Rows &p_rows, int64_t p_age) const {
    if (p_age < 0 || p_age >= p_rows.count) {
        return Variant();
    }
    return p_rows.table != nullptr
        ? p_rows.table->history_value(p_rows.row, uint32_t(p_age), column)
        : ring.value_of_age(p_age);
}

uint32_t History::newest_held(
    uint32_t p_cap,
    int64_t *r_ticks,
    Variant *r_values
) const {
    const Rows held = rows();
    const uint32_t count = uint32_t(std::min(held.count, int64_t(p_cap)));
    for (uint32_t age = 0; age < count; ++age) {
        r_ticks[age] = tick_in(held, age);
        r_values[age] = value_in(held, age);
    }
    return count;
}

Vector2i History::bracket_in(const Rows &p_rows, int64_t p_tick) const {
    if (p_rows.table == nullptr) {
        return ring.bracketing_ticks(p_tick);
    }
    int64_t previous = -1;
    int64_t next = -1;
    for (int64_t age = 0; age < p_rows.count; ++age) {
        const int64_t stored = tick_in(p_rows, age);
        if (stored <= p_tick) {
            previous = std::max(previous, stored);
        } else if (next == -1 || stored < next) {
            next = stored;
        }
    }
    return Vector2i(int32_t(previous), int32_t(next));
}

Variant History::at_in(const Rows &p_rows, int64_t p_tick) const {
    if (p_rows.table == nullptr) {
        return ring.get_at(p_tick);
    }
    for (int64_t age = 0; age < p_rows.count; ++age) {
        if (tick_in(p_rows, age) == p_tick) {
            return value_in(p_rows, age);
        }
    }
    return Variant();
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

void History::follow(
    const store::World *p_world,
    uint64_t p_owner,
    uint32_t p_lane,
    uint32_t p_column
) {
    if (follows(p_owner, p_lane, p_column) && world == p_world) {
        return;
    }
    ring.release();
    world = p_world;
    owner = p_owner;
    lane = p_lane;
    column = p_column;
    tick_domain = -1;
    has_recorded = false;
    source_rested = false;
    sleeping = false;
    floor_pending = false;
    const Rows held = rows();
    const uint32_t appends
        = held.table != nullptr ? held.table->history_appends[held.row] : 0;
    floor_append = appends > 0 ? appends - 1 : 0;
    seen_append = appends;
}

void History::unfollow() {
    world = nullptr;
    clear();
}

bool History::table_pulls() const {
    const Rows held = rows();
    return held.table != nullptr && held.table->scalar
        && held.table->sources[held.row] != uint8_t(store::Source::REMOTE);
}

bool History::table_rests() const {
    const Rows held = rows();
    if (held.table == nullptr
        || held.table->columns[column].body == store::BodyKey::NONE) {
        return false;
    }
    return (held.table->flags[held.row] & store::Table::RESTED) != 0;
}

void History::notice() {
    if (world == nullptr) {
        return;
    }
    if (floor_pending) {
        clear();
        return;
    }
    const Rows held = rows();
    if (held.table == nullptr) {
        return;
    }
    const uint32_t appends = held.table->history_appends[held.row];
    if (appends != seen_append) {
        seen_append = appends;
        sleeping = false;
    }
}

void History::record(int64_t tick, const Variant &value, bool authoring_tick) {
    NETW_ZONE_LEAF(
        "Display history record",
        colors::INTERP,
        profile::SUBSYSTEM_INTERPOLATION
    );
    if (world != nullptr) {
        return;
    }
    source_rested = false;
    const int domain = authoring_tick ? 1 : 0;
    if (tick_domain == -1) {
        tick_domain = domain;
    } else {
        NETW_ERR_COND(
            tick_domain != domain,
            sys::INTERPOLATION,
            "An interpolated value was dropped, because it mixed ticks from "
            "this peer with ticks from the server."
        );
    }

    const int type = value.get_type();
    if (value_type == Variant::NIL) {
        value_type = type;
    } else {
        NETW_ERR_COND(
            value_type != type,
            sys::INTERPOLATION,
            "An interpolated property changed its type, so the value was "
            "dropped. Give the property a static type."
        );
    }

    if (has_recorded && value == last_recorded) {
        return;
    }
    ring.record(tick, value);
    last_recorded = value;
    has_recorded = true;
    sleeping = false;
}

void History::record_every_tick(int64_t tick, const Variant &value) {
    if (world == nullptr && has_recorded && value != last_recorded
        && ring.size() > 0 && ring.tick_of_age(0) < tick - 1) {
        ring.record(tick - 1, last_recorded);
    }
    record(tick, value, false);
}

void History::clear() {
    ring.clear();
    tick_domain = -1;
    has_recorded = false;
    source_rested = false;
    sleeping = false;
    if (world == nullptr) {
        return;
    }
    floor_pending = false;
    const Rows held = rows();
    if (held.table == nullptr) {
        floor_pending = true;
        return;
    }
    floor_append = held.table->history_appends[held.row];
    seen_append = floor_append;
}

bool History::is_empty() const {
    return rows().count == 0;
}

int64_t History::size() const {
    return rows().count;
}

int64_t History::newest_tick() const {
    return tick_in(rows(), 0);
}

bool History::has_tick_after(int64_t tick) const {
    return newest_tick() > tick;
}

Vector2i History::bracketing_ticks(int64_t tick) const {
    return bracket_in(rows(), tick);
}

Variant History::get_at(int64_t tick) const {
    return at_in(rows(), tick);
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
    NETW_ZONE_LEAF(
        "Display history sample",
        colors::INTERP,
        profile::SUBSYSTEM_INTERPOLATION
    );
    projected = false;
    snap_taken = false;

    const Rows held = rows();
#if defined(NETW_TESTS)
    const SampleMeter meter(held.table != nullptr);
#endif
    const Vector2i bracket = bracket_in(held, dt);
    const int64_t previous_tick = bracket.x;
    const int64_t next_tick = bracket.y;
    if (previous_tick == -1) {
        return last_written;
    }

    if (next_tick == -1) {
        const Variant result = at_in(held, previous_tick);
        const bool finished = finished_in(held, dt);
        if (forecast && !finished && project::supports(result.get_type())) {
            const Variant velocity = has_explicit_velocity
                ? explicit_velocity
                : finite_velocity(held, previous_tick, ticktime);
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
        const bool stated = finished || received_in(held) < 0;
        if (!(tick_in(held, 0) > previous_tick)
            && is_close(last_written, result) && stated) {
            sleeping = true;
        }
        return result;
    }

    const Variant previous = at_in(held, previous_tick);
    const Variant next = at_in(held, next_tick);
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

Variant History::finite_velocity(
    const Rows &p_rows,
    int64_t newest,
    double ticktime
) const {
    if (ticktime <= 0.0) {
        return Variant();
    }
    const int64_t older = bracket_in(p_rows, newest - 1).x;
    if (older == -1 || older == newest) {
        return Variant();
    }
    const double span = double(newest - older) * ticktime;
    if (span <= 0.0) {
        return Variant();
    }

    const Variant recent = at_in(p_rows, newest);
    const Variant earlier = at_in(p_rows, older);
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
