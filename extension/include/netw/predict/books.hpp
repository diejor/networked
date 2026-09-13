#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/frames.hpp"

namespace netw::predict {

class TransitionKeys {
    godot::LocalVector<int64_t> keys;

public:
    int index_of(int64_t p_transition) const;
    int append(int64_t p_transition);
    int oldest() const;
    int64_t at(int p_index) const;
    void remove_at(int p_index);
    void clear();

    int size() const {
        return int(keys.size());
    }
};

class ClaimBook {
    TransitionKeys keys;
    godot::LocalVector<CommandEvidenceWire> claims;

public:
    void record(int64_t p_transition, const CommandEvidenceWire &p_claim);
    bool take(int64_t p_transition, CommandEvidenceWire &r_claim);
    void trim(int p_limit);
    void clear();

    int size() const {
        return keys.size();
    }
};

class WitnessClassBook {
    TransitionKeys keys;
    godot::LocalVector<int8_t> classes;

public:
    void record(int64_t p_transition, int p_witness_class, int p_limit);
    int at(int64_t p_transition) const;
    void clear();

    int size() const {
        return keys.size();
    }
};

class WitnessDetailBook {
    TransitionKeys keys;
    godot::LocalVector<godot::Dictionary> details;

public:
    void record(
        int64_t p_transition,
        const godot::Dictionary &p_detail,
        int p_limit
    );
    godot::Dictionary rows() const;
    void clear();

    int size() const {
        return keys.size();
    }
};

struct DeferredOperator {
    int64_t basis = -1;
    int64_t recv_tick = -1;
    godot::Dictionary payload;

    void hold(
        int64_t p_basis,
        int64_t p_recv_tick,
        const godot::Dictionary &p_payload
    );
    void release();

    bool held() const {
        return basis >= 0;
    }
};

class ContractionLedger {
    godot::LocalVector<int> fields;
    godot::LocalVector<double> errors;
    godot::LocalVector<int64_t> bases;

    int index_of(int p_field) const;

public:
    void arm(int p_field, double p_error, int64_t p_basis);
    godot::LocalVector<int> settle(
        int64_t p_ack,
        const godot::PackedFloat64Array &p_divergence
    );
    void clear();

    bool armed() const {
        return !fields.is_empty();
    }
};

} // namespace netw::predict
