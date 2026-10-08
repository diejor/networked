#include "netw/predict/recovery.hpp"

#include <algorithm>
#include <cmath>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/project.hpp"

using namespace godot;

namespace netw::predict {

namespace {

constexpr double TAU = 6.2831853071795864769252867666;
constexpr int ESCALATE_NONSHRINK = 3;

void copy_row_into(const StateRow &p_source, int p_width, StateRow &r_row) {
    r_row.resize(p_width);
    for (int at = 0; at < p_width; ++at) {
        if (p_source.has(at)) {
            r_row.set(at, p_source.values[uint32_t(at)]);
        } else {
            r_row.values[uint32_t(at)] = Variant();
        }
    }
}

double epsilon_at(const Wiring &p_wiring, int p_field, double p_fallback) {
    const double declared = p_wiring.epsilon[uint32_t(p_field)];
    return declared >= 0.0 ? declared : p_fallback;
}

double teleport_at(const Wiring &p_wiring, int p_field, double p_fallback) {
    const double declared = p_wiring.teleport[uint32_t(p_field)];
    return declared >= 0.0 ? declared : p_fallback;
}

double field_error(const RecoveryRequest &p_request, int p_field) {
    if (p_field < 0 || p_field >= int(p_request.field_errors.size())) {
        return -1.0;
    }
    return p_request.field_errors[uint32_t(p_field)];
}

double field_error_or(
    const RecoveryRequest &p_request,
    int p_field,
    double p_fallback
) {
    if (p_field < 0 || p_field >= int(p_request.field_errors.size())) {
        return p_fallback;
    }
    const double found = p_request.field_errors[uint32_t(p_field)];
    return found >= 0.0 ? found : p_fallback;
}

double angle_difference(double p_from, double p_to) {
    const double diff = std::fmod(p_to - p_from, TAU);
    return std::fmod(2.0 * diff, TAU) - diff;
}

Variant pose_delta(
    const Variant &p_target,
    const Variant &p_current,
    bool p_angle
) {
    switch (p_target.get_type()) {
        case Variant::FLOAT:
            return p_angle
                ? angle_difference(double(p_current), double(p_target))
                : double(p_target) - double(p_current);
        case Variant::VECTOR2:
            return Vector2(p_target) - Vector2(p_current);
        case Variant::VECTOR3:
            return Vector3(p_target) - Vector3(p_current);
        default:
            return Variant();
    }
}

Variant pose_scale(const Variant &p_value, double p_factor) {
    switch (p_value.get_type()) {
        case Variant::FLOAT:
            return double(p_value) * p_factor;
        case Variant::VECTOR2:
            return Vector2(p_value) * real_t(p_factor);
        case Variant::VECTOR3:
            return Vector3(p_value) * real_t(p_factor);
        default:
            return p_value;
    }
}

Variant pose_sum(const Variant &p_left, const Variant &p_right) {
    switch (p_left.get_type()) {
        case Variant::FLOAT:
            return double(p_left) + double(p_right);
        case Variant::VECTOR2:
            return Vector2(p_left) + Vector2(p_right);
        case Variant::VECTOR3:
            return Vector3(p_left) + Vector3(p_right);
        default:
            return p_left;
    }
}

Variant carried_value(
    const Variant &p_authority,
    const Variant &p_predicted,
    const Variant &p_current,
    bool p_angle
) {
    const Variant::Type type = p_authority.get_type();
    if (p_predicted.get_type() != type || p_current.get_type() != type) {
        return Variant();
    }
    switch (type) {
        case Variant::FLOAT:
        case Variant::VECTOR2:
        case Variant::VECTOR3:
            return pose_sum(
                p_current,
                pose_delta(p_authority, p_predicted, p_angle)
            );
        case Variant::QUATERNION:
            return (Quaternion(p_authority) * Quaternion(p_predicted).inverse()
                    * Quaternion(p_current))
                .normalized();
        case Variant::BASIS:
            return Basis(p_authority) * Basis(p_predicted).inverse()
                * Basis(p_current);
        case Variant::TRANSFORM3D: {
            const Transform3D authority = p_authority;
            const Transform3D predicted = p_predicted;
            const Transform3D current = p_current;
            return Transform3D(
                authority.basis * predicted.basis.inverse() * current.basis,
                current.origin + authority.origin - predicted.origin
            );
        }
        case Variant::TRANSFORM2D: {
            const Transform2D authority = p_authority;
            const Transform2D predicted = p_predicted;
            const Transform2D current = p_current;
            const real_t turn = real_t(angle_difference(
                predicted.get_rotation(),
                authority.get_rotation()
            ));
            Transform2D out = current;
            out.set_rotation(current.get_rotation() + turn);
            out.set_origin(
                current.get_origin() + authority.get_origin()
                - predicted.get_origin()
            );
            return out;
        }
        default:
            return Variant();
    }
}

double teleport_error(
    const Wiring &p_wiring,
    const RecoveryRequest &p_request,
    int p_field
) {
    if (p_request.authority.has(p_field) && p_request.predicted.has(p_field)) {
        return value_error(
            p_request.authority.values[uint32_t(p_field)],
            p_request.predicted.values[uint32_t(p_field)],
            p_wiring.angle[uint32_t(p_field)] != 0
        );
    }
    return field_error_or(p_request, p_field, -1.0);
}

bool teleport_reached(
    const Wiring &p_wiring,
    const RecoveryRequest &p_request
) {
    for (int at = 0; at < p_wiring.count(); ++at) {
        const double error = teleport_error(p_wiring, p_request, at);
        if (p_wiring.pose[uint32_t(at)] != 0 && error >= 0.0
            && error
                >= teleport_at(p_wiring, at, p_request.fallback_teleport)) {
            return true;
        }
    }
    return false;
}

bool project_restore(
    const Wiring &p_wiring,
    const RecoveryRequest &p_request,
    StateRow &r_restore
) {
    const int age_ticks = std::clamp(
        p_request.ack_age_ticks,
        0,
        std::max(0, p_request.max_restore_ticks)
    );
    const double age = double(age_ticks) * p_request.tick_delta;
    bool projected = false;
    for (int at = 0; at < p_wiring.count(); ++at) {
        const int channel = p_wiring.projection[uint32_t(at)];
        if (channel < 0 || !r_restore.has(at) || !r_restore.has(channel)) {
            continue;
        }
        const double projected_error
            = field_error_or(p_request, channel, 0.0) * age;
        if (projected_error
            >= epsilon_at(p_wiring, at, p_request.fallback_epsilon)) {
            continue;
        }
        const Variant value = r_restore.values[uint32_t(at)];
        if (!project::supports(value.get_type())) {
            continue;
        }
        r_restore.set(
            at,
            project::forward(value, r_restore.values[uint32_t(channel)], age)
        );
        projected = true;
    }
    return projected;
}

void remove_withheld(const Wiring &p_wiring, StateRow &r_restore) {
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (p_wiring.withheld[uint32_t(at)] != 0) {
            r_restore.present[uint32_t(at)] = 0;
        }
    }
}

void converge(
    const Wiring &p_wiring,
    const StateRow &p_current,
    StateRow &r_restore
) {
    for (int at = 0; at < p_wiring.count(); ++at) {
        const double rate = p_wiring.converge_rate[uint32_t(at)];
        if (rate <= 0.0 || rate >= 1.0 || !r_restore.has(at)
            || !p_current.has(at)) {
            continue;
        }
        const Variant delta = pose_delta(
            r_restore.values[uint32_t(at)],
            p_current.values[uint32_t(at)],
            p_wiring.angle[uint32_t(at)] != 0
        );
        if (delta.get_type() == Variant::NIL) {
            continue;
        }
        r_restore.set(
            at,
            pose_sum(
                p_current.values[uint32_t(at)],
                pose_scale(delta, std::clamp(rate, 0.0, 1.0))
            )
        );
    }
}

void direction_of(
    const Wiring &p_wiring,
    const RecoveryRequest &p_request,
    int p_field,
    int &r_axis,
    int &r_sign
) {
    r_axis = -1;
    r_sign = 0;
    if (p_field < 0) {
        return;
    }
    const Variant delta = pose_delta(
        p_request.authority.values[uint32_t(p_field)],
        p_request.predicted.values[uint32_t(p_field)],
        p_wiring.angle[uint32_t(p_field)] != 0
    );
    double component = 0.0;
    switch (delta.get_type()) {
        case Variant::FLOAT:
            r_axis = 0;
            component = double(delta);
            break;
        case Variant::VECTOR2: {
            const Vector2 value = delta;
            r_axis = std::abs(value.x) >= std::abs(value.y) ? 0 : 1;
            component = value[r_axis];
        } break;
        case Variant::VECTOR3: {
            const Vector3 value = delta;
            r_axis = 0;
            if (std::abs(value.y) > std::abs(value[r_axis])) {
                r_axis = 1;
            }
            if (std::abs(value.z) > std::abs(value[r_axis])) {
                r_axis = 2;
            }
            component = value[r_axis];
        } break;
        default:
            return;
    }
    r_sign = component > 0.0 ? 1 : (component < 0.0 ? -1 : 0);
}

void track_recovery(
    const Wiring &p_wiring,
    const Config &p_config,
    const RecoveryRequest &p_request,
    const WritePlan &p_plan,
    RecoveryState &r_state
) {
    if (p_plan.escalated) {
        r_state.reset_trackers();
        r_state.cooldown_until = p_request.current_label
            + std::max(0, p_request.collision_cooldown_ticks);
        return;
    }
    const bool skip
        = p_request.answered ? p_request.answered_skip : p_plan.skip;
    const bool teleport
        = p_request.answered ? p_request.answered_teleport : p_plan.teleport;
    if (skip || p_config.correction == int(CorrectionMode::REPLAY)) {
        return;
    }
    if (teleport) {
        r_state.reset_trackers();
        return;
    }

    const int field = escalation_field(p_wiring, p_request);
    int axis = -1;
    int sign = 0;
    direction_of(p_wiring, p_request, field, axis, sign);
    const bool same = field == r_state.last_field && axis == r_state.last_axis;
    const double divergence = field >= 0 ? field_error(p_request, field) : 0.0;
    const double previous = same ? r_state.last_divergence : -1.0;
    if (previous >= 0.0 && divergence < previous) {
        r_state.nonshrink_streak = 0;
        r_state.last_sign = 0;
        r_state.escalate_next = false;
    } else {
        const int grown = r_state.nonshrink_streak + 1;
        const bool flipped = same && sign != 0 && r_state.last_sign != 0
            && sign != r_state.last_sign;
        r_state.escalate_next = grown >= ESCALATE_NONSHRINK || flipped;
        r_state.nonshrink_streak = r_state.escalate_next ? 0 : grown;
        r_state.last_sign = sign;
    }
    r_state.last_field = field;
    r_state.last_axis = axis;
    r_state.last_divergence = divergence;
}

} // namespace

bool carry_difference(
    const Wiring &p_wiring,
    const StateRow &p_authority,
    const StateRow &p_predicted,
    const StateRow &p_current,
    StateRow &r_restore
) {
    bool carried = false;
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (!p_authority.has(at) || !p_predicted.has(at)
            || !p_current.has(at)) {
            continue;
        }
        const Variant value = carried_value(
            p_authority.values[uint32_t(at)],
            p_predicted.values[uint32_t(at)],
            p_current.values[uint32_t(at)],
            p_wiring.angle[uint32_t(at)] != 0
        );
        if (value.get_type() == Variant::NIL) {
            continue;
        }
        r_restore.set(at, value);
        carried = true;
    }
    return carried;
}

int escalation_field(const Wiring &p_wiring, const RecoveryRequest &p_request) {
    int dominant = -1;
    int exact = -1;
    double top = 0.0;
    double exact_top = 0.0;
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (p_wiring.causal[uint32_t(at)] == 0
            || p_wiring.trigger_exclude[uint32_t(at)] != 0
            || !p_request.predicted.has(at) || !p_request.authority.has(at)) {
            continue;
        }
        const double error = field_error(p_request, at);
        const double epsilon
            = epsilon_at(p_wiring, at, p_request.fallback_epsilon);
        if (error <= epsilon) {
            continue;
        }
        if (epsilon <= 0.0) {
            if (error > exact_top) {
                exact_top = error;
                exact = at;
            }
            continue;
        }
        const double ratio = error / epsilon;
        if (ratio > top) {
            top = ratio;
            dominant = at;
        }
    }
    if (dominant >= 0) {
        return dominant;
    }
    if (exact >= 0) {
        return exact;
    }
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (field_error(p_request, at) > top && p_request.predicted.has(at)
            && p_request.authority.has(at)) {
            top = field_error(p_request, at);
            dominant = at;
        }
    }
    return dominant;
}

void RecoveryState::reset_trackers() {
    nonshrink_streak = 0;
    last_field = -1;
    last_axis = -1;
    last_sign = 0;
    last_divergence = -1.0;
    escalate_next = false;
}

void RecoveryState::open_window(int64_t p_label, int p_cooldown) {
    window_until = std::max(
        window_until,
        p_label + int64_t(std::max(0, p_cooldown)) + 1
    );
}

void RecoveryState::suppress_until(int64_t p_label, int p_cooldown) {
    cooldown_until
        = std::max(cooldown_until, p_label + int64_t(std::max(0, p_cooldown)));
}

bool RecoveryState::window_contains(int64_t p_label) const {
    return window_until >= 0 && p_label < window_until;
}

bool RecoveryState::suppressed_at(int64_t p_label) const {
    return cooldown_until >= 0 && p_label < cooldown_until;
}

WritePlan stage_recovery(
    const Wiring &p_wiring,
    const Config &p_config,
    const RecoveryRequest &p_request,
    RecoveryState &r_state
) {
    WritePlan out;
    stage_recovery_into(p_wiring, p_config, p_request, r_state, out);
    return out;
}

void stage_recovery_into(
    const Wiring &p_wiring,
    const Config &p_config,
    const RecoveryRequest &p_request,
    RecoveryState &r_state,
    WritePlan &r_out
) {
    NETW_ZONE_NC("NetwPredict stage recovery", colors::PREDICTION);
    NETW_ZONE_VALUE(p_wiring.count());
    const int width = p_wiring.count();
    r_out.basis = p_request.basis;
    r_out.replay_from = -1;
    r_out.replay_through = -1;
    r_out.op = Operator::NONE;
    r_out.teleport = false;
    r_out.skip = true;
    r_out.restore.resize(width);
    r_out.write.resize(width);
    r_out.escalated = r_state.escalate_next;
    if (p_request.policy == int(RecoveryPolicy::OBSERVE)) {
        return;
    }

    copy_row_into(p_request.authority, width, r_out.restore);
    if (p_config.correction == int(CorrectionMode::REPLAY)) {
        r_out.op = Operator::REBASE_EXACT;
        r_out.skip = false;
        track_recovery(p_wiring, p_config, p_request, r_out, r_state);
        return;
    }

    const bool carried = carry_difference(
        p_wiring,
        p_request.authority,
        p_request.predicted,
        p_request.current,
        r_out.restore
    );
    bool projected = false;
    if (!carried && p_config.restore == int(RestoreMode::EXTRAPOLATED)) {
        projected = project_restore(p_wiring, p_request, r_out.restore);
    }
    if (r_out.escalated || p_request.pose_unmeasured
        || teleport_reached(p_wiring, p_request)) {
        copy_row_into(r_out.restore, width, r_out.write);
        r_out.op = Operator::FULL_CLOSURE;
        r_out.teleport = true;
        r_out.skip = false;
        track_recovery(p_wiring, p_config, p_request, r_out, r_state);
        return;
    }

    const bool full = p_request.domain == Domain::OUT_OF_DOMAIN
        || (p_request.attribution == Attribution::UNKNOWN
            && p_request.contact_window);
    if (full) {
        copy_row_into(r_out.restore, width, r_out.write);
        r_out.op
            = projected ? Operator::REBASE_PROJECTED : Operator::REBASE_EXACT;
        r_out.skip = false;
        track_recovery(p_wiring, p_config, p_request, r_out, r_state);
        return;
    }
    if (p_request.suppressed) {
        return;
    }

    remove_withheld(p_wiring, r_out.restore);
    converge(p_wiring, p_request.current, r_out.restore);
    copy_row_into(r_out.restore, width, r_out.write);
    r_out.op = projected ? Operator::REBASE_PROJECTED : Operator::REBASE_EXACT;
    r_out.skip = false;
    track_recovery(p_wiring, p_config, p_request, r_out, r_state);
}

TransportPlan transport(
    const Wiring &p_wiring,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const StateRow &p_current
) {
    NETW_ZONE_NC("NetwPredict transport", colors::PREDICTION);
    NETW_ZONE_VALUE(p_wiring.count());
    TransportPlan out;
    out.restore.resize(p_wiring.count());
    out.delta.resize(p_wiring.count());
    bool any = false;
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (p_wiring.pose[uint32_t(at)] == 0) {
            continue;
        }
        if (!p_predicted.has(at) || !p_authority.has(at)
            || !p_current.has(at)) {
            return out;
        }
        const Variant delta = pose_delta(
            p_authority.values[uint32_t(at)],
            p_predicted.values[uint32_t(at)],
            p_wiring.angle[uint32_t(at)] != 0
        );
        if (delta.get_type() == Variant::NIL) {
            return out;
        }
        out.delta.set(at, delta);
        out.restore.set(at, pose_sum(p_current.values[uint32_t(at)], delta));
        any = true;
    }
    out.valid = any;
    return out;
}

WritePlan dissipate_plan(int p_width, int64_t p_basis) {
    WritePlan out;
    out.restore.resize(p_width);
    out.write.resize(p_width);
    out.basis = p_basis;
    out.op = Operator::DISSIPATE;
    out.skip = false;
    return out;
}

StateRow advance_seed(
    const Wiring &p_wiring,
    const Config &p_config,
    const StateRow &p_payload,
    int p_transition_span,
    double p_tick_delta
) {
    StateRow out;
    copy_row_into(p_payload, p_wiring.count(), out);
    if (p_config.restore != int(RestoreMode::EXTRAPOLATED)) {
        return out;
    }
    const int span = std::clamp(
        p_transition_span,
        0,
        std::max(0, p_config.max_restore_ticks)
    );
    if (span == 0) {
        return out;
    }
    const double age = double(span) * p_tick_delta;
    for (int at = 0; at < p_wiring.count(); ++at) {
        const int channel = p_wiring.projection[uint32_t(at)];
        if (channel < 0 || !out.has(at) || !out.has(channel)) {
            continue;
        }
        const Variant value = out.values[uint32_t(at)];
        if (!project::supports(value.get_type())) {
            continue;
        }
        out.set(
            at,
            project::forward(value, out.values[uint32_t(channel)], age)
        );
    }
    return out;
}

bool transport_admissible(const TransportEvidence &p_evidence) {
    NETW_ZONE_NC("NetwPredict transport gate", colors::PREDICTION);
    const bool out = p_evidence.candidate && p_evidence.basis_witness_clean
        && p_evidence.recent_witness_clean && p_evidence.non_pose_agrees
        && p_evidence.below_teleport && !p_evidence.escalated
        && p_evidence.snap_correction && !p_evidence.observing;
    NETW_TRACE(
        sys::PREDICTION,
        "Moving the correction allowed %d (candidate %d, contacts clean %d "
        "and %d, other state agrees %d, below teleport %d, escalated %d, "
        "snap %d, observing %d).",
        int(out),
        int(p_evidence.candidate),
        int(p_evidence.basis_witness_clean),
        int(p_evidence.recent_witness_clean),
        int(p_evidence.non_pose_agrees),
        int(p_evidence.below_teleport),
        int(p_evidence.escalated),
        int(p_evidence.snap_correction),
        int(p_evidence.observing)
    );
    return out;
}

bool dissipate_admissible(const DissipateEvidence &p_evidence) {
    NETW_ZONE_NC("NetwPredict dissipate gate", colors::PREDICTION);
    const bool out = p_evidence.momentum_active && !p_evidence.other_active
        && p_evidence.basis_witness_clean && p_evidence.recent_witness_clean
        && !p_evidence.escalated && p_evidence.snap_correction
        && !p_evidence.observing && p_evidence.meter > 0;
    NETW_TRACE(
        sys::PREDICTION,
        "Fading the correction allowed %d (momentum %d, other motion %d, "
        "contacts clean %d and %d, escalated %d, snap %d, observing %d, "
        "budget %d).",
        int(out),
        int(p_evidence.momentum_active),
        int(p_evidence.other_active),
        int(p_evidence.basis_witness_clean),
        int(p_evidence.recent_witness_clean),
        int(p_evidence.escalated),
        int(p_evidence.snap_correction),
        int(p_evidence.observing),
        p_evidence.meter
    );
    return out;
}

void apply_restore(StateRow &r_state, const StateRow &p_restore) {
    if (r_state.values.size() != p_restore.values.size()) {
        r_state.resize(int(p_restore.values.size()));
    }
    for (int at = 0; at < int(p_restore.values.size()); ++at) {
        if (p_restore.has(at)) {
            r_state.set(at, p_restore.values[uint32_t(at)]);
        }
    }
}

} // namespace netw::predict
