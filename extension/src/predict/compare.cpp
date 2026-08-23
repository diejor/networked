#include "netw/predict/compare.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace godot;

namespace netw {

namespace predict {

StateRow::StateRow(const StateRow &p_other) {
    *this = p_other;
}

StateRow &StateRow::operator=(const StateRow &p_other) {
    if (this == &p_other) {
        return *this;
    }
    values.resize(p_other.values.size());
    present.resize(p_other.present.size());
    for (uint32_t at = 0; at < p_other.values.size(); ++at) {
        values[at] = p_other.values[at];
        present[at] = p_other.present[at];
    }
    return *this;
}

namespace {

constexpr double TAU = 6.2831853071795864769252867666;

bool is_number(Variant::Type p_type) {
    return p_type == Variant::INT || p_type == Variant::FLOAT;
}

double angle_difference(double p_from, double p_to) {
    const double diff = std::fmod(p_to - p_from, TAU);
    return std::fmod(2.0 * diff, TAU) - diff;
}

bool has_causal_field(const Wiring &p_wiring) {
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (p_wiring.causal[uint32_t(at)] != 0) {
            return true;
        }
    }
    return false;
}

double tolerance_at(
    const LocalVector<double> &p_tolerances,
    int p_field,
    double p_fallback
) {
    if (p_field < int(p_tolerances.size())
        && p_tolerances[uint32_t(p_field)] >= 0.0) {
        return p_tolerances[uint32_t(p_field)];
    }
    return p_fallback;
}

int measure(
    const LocalVector<double> &p_errors,
    const LocalVector<double> &p_tolerances
) {
    int meter = 0;
    for (int at = 0; at < int(p_tolerances.size()); ++at) {
        const double tolerance = p_tolerances[uint32_t(at)];
        if (tolerance < 0.0) {
            continue;
        }
        const double error = at < int(p_errors.size())
            ? p_errors[uint32_t(at)]
            : std::numeric_limits<double>::infinity();
        if (!std::isfinite(error) || error < 0.0) {
            return METER_SATURATED;
        }
        if (tolerance <= 0.0) {
            meter = std::max(meter, error > 0.0 ? 1 : 0);
            continue;
        }
        const double over = std::max(error - tolerance, 0.0);
        meter = std::max(meter, int(std::ceil(over / tolerance)));
    }
    return meter;
}

ExactVerdict exact_of(uint8_t p_flags) {
    if ((p_flags & ROW_ACKED) == 0) {
        return ExactVerdict::UNJUDGED;
    }
    return (p_flags & ROW_MATCHED) != 0 ? ExactVerdict::EQUAL
                                        : ExactVerdict::UNEQUAL;
}

DifferingFamily differing_family(
    const FamilyFingerprints &p_local,
    const FamilyFingerprints &p_peer
) {
    if (p_local.pose != p_peer.pose) {
        return DifferingFamily::POSE;
    }
    if (p_local.momentum != p_peer.momentum) {
        return DifferingFamily::MOMENTUM;
    }
    if (p_local.controller != p_peer.controller) {
        return DifferingFamily::CONTROLLER_LATCH;
    }
    return DifferingFamily::NONE;
}

} // namespace

void StateRow::resize(int p_count) {
    const uint32_t count = uint32_t(p_count < 0 ? 0 : p_count);
    values.resize(count);
    present.resize(count);
    for (uint32_t at = 0; at < count; ++at) {
        present[at] = 0;
    }
}

void StateRow::set(int p_field, const Variant &p_value) {
    if (p_field < 0 || p_field >= int(values.size())) {
        return;
    }
    values[uint32_t(p_field)] = p_value;
    present[uint32_t(p_field)] = 1;
}

bool StateRow::has(int p_field) const {
    return p_field >= 0 && p_field < int(present.size())
        && present[uint32_t(p_field)] != 0;
}

bool StateRow::any() const {
    for (uint32_t at = 0; at < present.size(); ++at) {
        if (present[at] != 0) {
            return true;
        }
    }
    return false;
}

double value_error(
    const Variant &p_left,
    const Variant &p_right,
    bool p_angle
) {
    const Variant::Type left_type = p_left.get_type();
    const Variant::Type right_type = p_right.get_type();
    if (p_angle && is_number(left_type) && is_number(right_type)) {
        return std::abs(angle_difference(double(p_left), double(p_right)));
    }
    if (left_type == Variant::VECTOR2 && right_type == Variant::VECTOR2) {
        return Vector2(p_left).distance_to(Vector2(p_right));
    }
    if (left_type == Variant::VECTOR3 && right_type == Variant::VECTOR3) {
        return Vector3(p_left).distance_to(Vector3(p_right));
    }
    if (is_number(left_type) && is_number(right_type)) {
        return std::abs(double(p_left) - double(p_right));
    }
    if (left_type == Variant::QUATERNION && right_type == Variant::QUATERNION) {
        return std::abs(Quaternion(p_left).angle_to(Quaternion(p_right)));
    }
    if (left_type == Variant::BASIS && right_type == Variant::BASIS) {
        return std::abs(Basis(p_left).get_rotation_quaternion().angle_to(
            Basis(p_right).get_rotation_quaternion()
        ));
    }
    if (left_type == Variant::TRANSFORM3D
        && right_type == Variant::TRANSFORM3D) {
        const Transform3D left = p_left;
        const Transform3D right = p_right;
        return left.origin.distance_to(right.origin)
            + std::abs(left.basis.get_rotation_quaternion().angle_to(
                right.basis.get_rotation_quaternion()
            ));
    }
    if (left_type == Variant::TRANSFORM2D
        && right_type == Variant::TRANSFORM2D) {
        const Transform2D left = p_left;
        const Transform2D right = p_right;
        return left.get_origin().distance_to(right.get_origin())
            + std::abs(
                   angle_difference(left.get_rotation(), right.get_rotation())
            );
    }
    return p_left == p_right ? 0.0 : std::numeric_limits<double>::infinity();
}

Attribution attribute(
    bool p_pre_equal,
    bool p_command_equal,
    bool p_environment_equal,
    bool p_topology_equal,
    bool p_raw_equal,
    bool p_witness_equal,
    int p_local_evidence,
    int p_peer_evidence,
    bool p_evidence_complete
) {
    if (!p_evidence_complete) {
        return Attribution::UNKNOWN;
    }
    if (!p_pre_equal) {
        return Attribution::PRE_STATE;
    }
    if (!p_command_equal) {
        return Attribution::COMMAND;
    }
    if (!p_environment_equal) {
        return Attribution::ENVIRONMENT;
    }
    if (!p_topology_equal) {
        return Attribution::TOPOLOGY;
    }
    const bool both_raw = (p_local_evidence & EVIDENCE_RAW) != 0
        && (p_peer_evidence & EVIDENCE_RAW) != 0;
    if (both_raw && !p_raw_equal) {
        return Attribution::EXECUTION;
    }
    const bool both_witness = (p_local_evidence & EVIDENCE_WITNESS) != 0
        && (p_peer_evidence & EVIDENCE_WITNESS) != 0;
    if (!both_witness) {
        return Attribution::UNKNOWN;
    }
    return p_witness_equal ? Attribution::CLOSURE : Attribution::CONTACT;
}

AckVerdict admit_ack(
    Journal &p_journal,
    int64_t p_transition,
    const EvidenceRow &p_peer,
    bool p_substituted
) {
    AckVerdict out;
    out.transition = p_transition;
    if (p_substituted) {
        p_journal.mark_substituted(p_transition);
        out.attribution = Attribution::COMMAND;
        return out;
    }

    const JournalEvidence local = p_journal.evidence_of(p_transition);
    if (!local.present) {
        return out;
    }
    out.compared = true;
    out.evidence_complete = p_peer.complete;
    const bool matched = local.post_fp == p_peer.post_fp;
    p_journal.mark_ack(p_transition, matched);
    out.exact = matched ? ExactVerdict::EQUAL : ExactVerdict::UNEQUAL;
    if (matched) {
        return out;
    }

    // Two peers that declared no witness both fingerprint zero, and an equality
    // between two absences is not a matched witness. The bit claims the local
    // witness detail describes the peer's too, which nothing recorded.
    const bool both_witness = p_peer.complete
        && (local.evidence_mask & EVIDENCE_WITNESS) != 0
        && (p_peer.evidence_mask & EVIDENCE_WITNESS) != 0;
    const bool witness_equal
        = both_witness && local.witness_fp == p_peer.witness_fp;
    p_journal.mark_witness_match(p_transition, witness_equal);
    out.attribution = attribute(
        p_peer.complete && local.pre_fp == p_peer.pre_fp,
        p_peer.complete && local.c_hash == p_peer.c_hash,
        p_peer.complete && local.e_digest == p_peer.e_digest,
        p_peer.complete && local.topo_fp == p_peer.topo_fp,
        p_peer.complete && local.raw_fp == p_peer.raw_fp,
        witness_equal,
        local.evidence_mask,
        p_peer.evidence_mask,
        p_peer.complete
    );
    p_journal.mark_attribution(p_transition, out.attribution);
    if (out.attribution == Attribution::PRE_STATE) {
        out.differing_family
            = differing_family(local.pre_families, p_peer.pre_families);
    } else if (out.attribution == Attribution::CLOSURE) {
        out.differing_family
            = differing_family(local.post_families, p_peer.post_families);
    }
    p_journal.mark_differing_family(p_transition, out.differing_family);
    return out;
}

StateVerdict compare_state(
    const Wiring &p_wiring,
    Journal &p_journal,
    int64_t p_recv_tick,
    int64_t p_transition,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const LocalVector<double> &p_correction_tolerances,
    const LocalVector<double> &p_meter_tolerances,
    double p_fallback_epsilon,
    bool p_stream_reconstructed
) {
    StateVerdict out;
    out.recv_tick = p_recv_tick;
    out.transition = p_transition;
    out.domain = p_journal.domain_of(p_transition);
    out.exact = exact_of(p_journal.flags_of(p_transition));
    out.attribution = p_journal.attribution_of(p_transition);
    out.field_errors.resize(uint32_t(p_wiring.count()));
    out.all_field_errors.resize(uint32_t(p_wiring.count()));
    for (uint32_t at = 0; at < out.field_errors.size(); ++at) {
        out.field_errors[at] = -1.0;
        out.all_field_errors[at] = -1.0;
    }
    if (!p_stream_reconstructed) {
        return out;
    }

    out.compared = true;
    out.divergence = 0.0;
    const bool causal_only = has_causal_field(p_wiring);
    bool compared_any = false;
    bool tolerance_diverged = false;
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (!p_authority.has(at)) {
            continue;
        }
        const double error = p_predicted.has(at)
            ? value_error(
                  p_predicted.values[uint32_t(at)],
                  p_authority.values[uint32_t(at)],
                  p_wiring.angle[uint32_t(at)] != 0
              )
            : std::numeric_limits<double>::infinity();
        out.all_field_errors[uint32_t(at)] = error;
        if (causal_only && p_wiring.causal[uint32_t(at)] == 0) {
            continue;
        }
        compared_any = true;
        out.field_errors[uint32_t(at)] = error;
        out.divergence = std::max(out.divergence, error);
        if (p_wiring.vote_exclude[uint32_t(at)] == 0
            && error > tolerance_at(
                   p_correction_tolerances,
                   at,
                   p_fallback_epsilon
               )) {
            tolerance_diverged = true;
        }
    }
    if (!compared_any || !p_predicted.any()) {
        out.divergence = std::numeric_limits<double>::infinity();
        tolerance_diverged = true;
    }

    const bool in_domain = out.domain == Domain::IN_DOMAIN;
    const bool exact = in_domain && out.exact != ExactVerdict::UNJUDGED;
    out.corrected
        = exact ? out.exact == ExactVerdict::UNEQUAL : tolerance_diverged;
    out.settled = !in_domain || out.exact != ExactVerdict::UNJUDGED;
    out.meter = measure(out.field_errors, p_meter_tolerances);
    if (out.corrected && out.meter == 0) {
        out.meter = 1;
    } else if (!out.corrected) {
        out.meter = 0;
    }
    if (out.settled) {
        p_journal.mark_aligned_error(p_transition, out.divergence);
    }
    return out;
}

} // namespace predict

} // namespace netw
