#pragma once

/* The recovery staged for one authoritative comparison.
 *
 * Recovery mutates the engine's dense state and publishes the same values as a
 * write plan. A shell drains the binding rows. A shell-less driver reads the
 * engine state and leaves the binding rows undrained.
 */

#include <cstdint>

#include "netw/predict/compare.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/wiring.hpp"

namespace netw::predict {

struct RecoveryRequest {
    StateRow predicted;
    StateRow authority;
    StateRow current;
    LocalVector<double> field_errors;
    int64_t basis = -1;
    int64_t current_label = -1;
    int policy = 0;
    double fallback_epsilon = 0.0;
    double fallback_teleport = 0.0;
    int max_restore_ticks = 0;
    int ack_age_ticks = 0;
    int collision_cooldown_ticks = 0;
    double tick_delta = 1.0 / 60.0;
    Domain domain = Domain::OUT_OF_DOMAIN;
    Attribution attribution = Attribution::UNKNOWN;
    bool contact_window = false;
    bool suppressed = false;
    bool pose_unmeasured = false;
};

struct WritePlan {
    StateRow restore;
    StateRow write;
    int64_t basis = -1;
    int64_t replay_from = -1;
    int64_t replay_through = -1;
    Operator op = Operator::NONE;
    bool teleport = false;
    bool skip = true;
    bool escalated = false;
};

struct TransportPlan {
    StateRow restore;
    StateRow delta;
    bool valid = false;
};

/* What a caller observed about a conditional operator's preconditions.
 *
 * Every field is a live tree fact a shell samples. The RULE over them lives
 * here, so two peers reading the same evidence decide alike whichever arm
 * gathered it.
 */
struct TransportEvidence {
    bool candidate = false;
    bool basis_witness_clean = false;
    bool recent_witness_clean = false;
    bool non_pose_agrees = false;
    bool below_teleport = false;
    bool escalated = false;
    bool snap_correction = false;
    bool observing = false;
};

struct DissipateEvidence {
    bool momentum_active = false;
    bool other_active = false;
    bool basis_witness_clean = false;
    bool recent_witness_clean = false;
    bool escalated = false;
    bool snap_correction = false;
    bool observing = false;
    int meter = 0;
};

// Every fact short of the corridor. The corridor is game code, so it is asked
// only after this answers true.
bool transport_admissible(const TransportEvidence &p_evidence);

bool dissipate_admissible(const DissipateEvidence &p_evidence);

struct RecoveryState {
    int nonshrink_streak = 0;
    int last_field = -1;
    int last_axis = -1;
    int last_sign = 0;
    double last_divergence = -1.0;
    int64_t cooldown_until = -1;
    int64_t window_until = -1;
    bool escalate_next = false;

    void reset_trackers();
    void open_window(int64_t p_label, int p_cooldown);
    void suppress_until(int64_t p_label, int p_cooldown);
    bool window_contains(int64_t p_label) const;
    bool suppressed_at(int64_t p_label) const;
};

struct RecoveryStats {
    int corrections = 0;
    int teleports = 0;
    int escalations = 0;
    int recoveries_skipped = 0;
};

WritePlan stage_recovery(
    const Wiring &p_wiring,
    const Config &p_config,
    const RecoveryRequest &p_request,
    RecoveryState &r_state
);

TransportPlan transport(
    const Wiring &p_wiring,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const StateRow &p_current
);

WritePlan dissipate_plan(int p_width, int64_t p_basis);

StateRow advance_seed(
    const Wiring &p_wiring,
    const Config &p_config,
    const StateRow &p_payload,
    int p_transition_span,
    double p_tick_delta
);

void apply_restore(StateRow &r_state, const StateRow &p_restore);

} // namespace netw::predict
