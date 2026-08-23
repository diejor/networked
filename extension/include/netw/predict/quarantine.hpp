#pragma once

/* The bounded proof that authority is coherent enough to resume prediction.
 *
 * State and witness lanes arrive independently. A clean consecutive witness
 * run licenses a recent state, while a finite wait permits an older retained
 * clean or unknown state and never one known dirty.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/predict/compare.hpp"

namespace netw::predict {

constexpr int QUARANTINE_RESUME_MULTIPLIER = 2;
constexpr int QUARANTINE_RESUME_MIN = 3;
constexpr int QUARANTINE_RUN_CAP = 256;
constexpr int QUARANTINE_PENDING_LIMIT = 256;
constexpr uint8_t WITNESS_SUPPORT = 1;
constexpr uint8_t WITNESS_STATIC = 2;

struct QuarantinePlan {
    StateRow payload;
    int64_t basis = -1;
    bool ready = false;
};

struct QuarantineState {
    StateRow payload;
    int64_t tick = -1;
    int64_t basis = -1;
};

struct QuarantineWitness {
    int64_t basis = -1;
    int bits = -1;
};

struct Quarantine {
    godot::LocalVector<QuarantineState> pending_states;
    godot::LocalVector<QuarantineWitness> witnesses;
    int clean_run = 0;
    int target = QUARANTINE_RESUME_MIN;
    int64_t last_tick = -1;
    int64_t last_basis = -1;
    int64_t wait_anchor = -1;
    int64_t reseed_basis = -1;
    int64_t aligned_transition = -1;
    int64_t ignore_through = -1;
    bool latched = false;
    bool stream_reconstructed = false;
    bool align_pending = false;
    bool epoch_confirmed = false;
    bool probation_pending = false;

    void enter(
        int p_ack_age,
        int p_flap_multiplier,
        bool p_stream_reconstructed
    );
    QuarantinePlan admit_state(
        int64_t p_tick,
        int64_t p_basis,
        const StateRow &p_payload,
        bool p_whole
    );
    QuarantinePlan admit_witness(int64_t p_basis, int p_bits);
    void confirm_epoch();
    QuarantinePlan align(
        int64_t p_transition,
        const StateRow &p_payload,
        int64_t p_ignore_through
    );
    bool admit_post_reseed(int64_t p_basis);
    void adopt_alignment(int64_t p_transition, int64_t p_ignore_through);
    bool finish_probation(bool p_corrected);
    bool corrections_suppressed() const;
};

} // namespace netw::predict
