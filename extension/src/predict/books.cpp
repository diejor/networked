#include "netw/predict/books.hpp"

#include <limits>

namespace netw::predict {

int TransitionKeys::index_of(int64_t p_transition) const {
    for (uint32_t at = 0; at < keys.size(); ++at) {
        if (keys[at] == p_transition) {
            return int(at);
        }
    }
    return -1;
}

int TransitionKeys::append(int64_t p_transition) {
    keys.push_back(p_transition);
    return int(keys.size()) - 1;
}

int TransitionKeys::oldest() const {
    if (keys.is_empty()) {
        return -1;
    }
    int oldest = 0;
    for (uint32_t at = 1; at < keys.size(); ++at) {
        if (keys[at] < keys[oldest]) {
            oldest = int(at);
        }
    }
    return oldest;
}

int64_t TransitionKeys::at(int p_index) const {
    return keys[p_index];
}

void TransitionKeys::remove_at(int p_index) {
    keys.remove_at(p_index);
}

void TransitionKeys::clear() {
    keys.clear();
}

void ClaimBook::record(
    int64_t p_transition,
    const CommandEvidenceWire &p_claim
) {
    const int at = keys.index_of(p_transition);
    if (at >= 0) {
        claims[at] = p_claim;
        return;
    }
    claims.push_back(p_claim);
    keys.append(p_transition);
}

bool ClaimBook::take(int64_t p_transition, CommandEvidenceWire &r_claim) {
    const int at = keys.index_of(p_transition);
    if (at < 0) {
        return false;
    }
    r_claim = claims[at];
    keys.remove_at(at);
    claims.remove_at(at);
    return true;
}

void ClaimBook::trim(int p_limit) {
    while (keys.size() > p_limit) {
        const int at = keys.oldest();
        if (at < 0) {
            return;
        }
        keys.remove_at(at);
        claims.remove_at(at);
    }
}

void ClaimBook::clear() {
    keys.clear();
    claims.clear();
}

void WitnessClassBook::record(
    int64_t p_transition,
    int p_witness_class,
    int p_limit
) {
    const int8_t bits = int8_t(p_witness_class);
    const int at = keys.index_of(p_transition);
    if (at >= 0) {
        classes[at] = bits;
        return;
    }
    classes.push_back(bits);
    keys.append(p_transition);
    while (keys.size() > p_limit) {
        const int oldest = keys.oldest();
        if (oldest < 0) {
            return;
        }
        keys.remove_at(oldest);
        classes.remove_at(oldest);
    }
}

int WitnessClassBook::at(int64_t p_transition) const {
    const int at = keys.index_of(p_transition);
    return at >= 0 ? int(classes[at]) : -1;
}

void WitnessClassBook::clear() {
    keys.clear();
    classes.clear();
}

void WitnessDetailBook::record(
    int64_t p_transition,
    const godot::Dictionary &p_detail,
    int p_limit
) {
    const godot::Dictionary held = p_detail.duplicate(true);
    const int at = keys.index_of(p_transition);
    if (at >= 0) {
        details[at] = held;
        return;
    }
    details.push_back(held);
    keys.append(p_transition);
    while (keys.size() > p_limit) {
        const int oldest = keys.oldest();
        if (oldest < 0) {
            return;
        }
        keys.remove_at(oldest);
        details.remove_at(oldest);
    }
}

godot::Dictionary WitnessDetailBook::rows() const {
    godot::Dictionary out;
    for (int at = 0; at < keys.size(); ++at) {
        out[keys.at(at)] = details[at];
    }
    return out;
}

void WitnessDetailBook::clear() {
    keys.clear();
    details.clear();
}

void DeferredOperator::hold(
    int64_t p_basis,
    int64_t p_recv_tick,
    const godot::Dictionary &p_payload
) {
    basis = p_basis;
    recv_tick = p_recv_tick;
    payload = p_payload.duplicate(true);
}

void DeferredOperator::release() {
    basis = -1;
    recv_tick = -1;
    payload = godot::Dictionary();
}

int ContractionLedger::index_of(int p_field) const {
    for (uint32_t at = 0; at < fields.size(); ++at) {
        if (fields[at] == p_field) {
            return int(at);
        }
    }
    return -1;
}

void ContractionLedger::arm(int p_field, double p_error, int64_t p_basis) {
    const int at = index_of(p_field);
    if (at >= 0) {
        errors[at] = p_error;
        bases[at] = p_basis;
        return;
    }
    fields.push_back(p_field);
    errors.push_back(p_error);
    bases.push_back(p_basis);
}

godot::LocalVector<int> ContractionLedger::settle(
    int64_t p_ack,
    const godot::PackedFloat64Array &p_divergence
) {
    godot::LocalVector<int> contracted;
    uint32_t kept = 0;
    for (uint32_t at = 0; at < fields.size(); ++at) {
        if (p_ack <= bases[at]) {
            fields[kept] = fields[at];
            errors[kept] = errors[at];
            bases[kept] = bases[at];
            kept += 1;
            continue;
        }
        const int field = fields[at];
        const double reached = field >= 0 && field < p_divergence.size()
            ? p_divergence[field]
            : std::numeric_limits<double>::infinity();
        if (reached < errors[at]) {
            contracted.push_back(field);
        }
    }
    fields.resize(kept);
    errors.resize(kept);
    bases.resize(kept);
    return contracted;
}

void ContractionLedger::clear() {
    fields.clear();
    errors.clear();
    bases.clear();
}

} // namespace netw::predict
