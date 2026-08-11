#pragma once

/* The bounded evidence for one actionable divergence.
 *
 * An episode pins its generator outside the journal ring, retains bounded
 * comparison and operator evidence, and either proves agreement or exhausts a
 * structural recovery budget. Quality settings cannot expand either budget.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/journal.hpp"

namespace netw::predict {

constexpr int EPISODE_CLOSE_RUN_MAX = 64;
constexpr int EPISODE_CLOSE_RUN_MIN = 3;
constexpr int EPISODE_FLAP_WINDOW = 64;
constexpr int EPISODE_EVIDENCE_LIMIT = JOURNAL_CAPACITY_DEFAULT;
constexpr int EPISODE_GENERATOR_LIMIT = 16;
constexpr int EPISODE_NON_CONTRACTION_BUDGET = 4;
constexpr int EPISODE_FULL_CLOSURE_BUDGET = 2;
constexpr int METER_UNMEASURED = -1;

enum class EpisodeState : uint8_t {
    OPEN = 0,
    CLOSED = 1,
    FALLBACK = 2,
};

enum class OperatorOutcome : int8_t {
    REFUSED = -1,
    PENDING = 0,
    CONTRACTED = 1,
    FAILED_TO_CONTRACT = 2,
    INTRODUCED_BOUNDARY = 3,
    WITHHELD = 4,
};

enum class TriggerShape : uint8_t {
    NONE = 0,
    MIXED = 1,
    ALL_WITHHELD = 2,
};

struct PinnedRow {
    JournalEvidence evidence;
    int64_t transition = -1;
    int64_t label = -1;
    Domain domain = Domain::IN_DOMAIN;
    Attribution attribution = Attribution::UNKNOWN;
    uint8_t flags = 0;
    bool beyond_retention = false;
    bool present = false;
};

struct EpisodeComparison {
    int64_t transition = -1;
    int meter = 0;
    int write_id = 0;
    bool agrees = false;
};

struct EpisodeWrite {
    int episode = 0;
    int write_id = 0;
    Operator op = Operator::NONE;
    int64_t basis = -1;
    int32_t delta_fp = 0;
    StringName target;
    OperatorOutcome outcome = OperatorOutcome::PENDING;
    int meter_before = 0x7FFFFFFF;
    int meter_after = METER_UNMEASURED;
    int verdicts = 0;
    int verify_run = EPISODE_CLOSE_RUN_MIN;
    int64_t judged_transition = -1;
    int best_meter = 0x7FFFFFFF;
    TriggerShape trigger_shape = TriggerShape::NONE;
    bool evidence_free = false;
    bool null_operator = false;
};

struct EpisodeDecision {
    Operator op = Operator::NONE;
    int64_t basis = -1;
    Dictionary eligibility;
    bool eligible = false;
    bool applied = false;
};

struct Episode {
    PinnedRow generator;
    LocalVector<EpisodeWrite> writes;
    LocalVector<int64_t> taint;
    LocalVector<PinnedRow> secondary_generators;
    LocalVector<EpisodeComparison> comparisons;
    LocalVector<EpisodeDecision> decisions;
    LocalVector<int> reopen_chain;

    int id = 0;
    int64_t opened_transition = -1;
    Attribution attribution = Attribution::UNKNOWN;
    EpisodeState state = EpisodeState::OPEN;
    int reopened_from = 0;
    int evidence_dropped = 0;
    int non_contraction_used = 0;
    int withheld_non_contractions = 0;
    int evidence_free_non_contractions = 0;
    int nc_no_trigger = 0;
    int nc_mixed_trigger = 0;
    int closure_used = 0;
    int agreement_run = 0;
    int64_t last_comparison_transition = -1;
    int agreement_write_id = 0;
    int last_write_id = 0;
    int64_t closed_transition = -1;
    int64_t fallback_transition = -1;
    int resume_ack_age = 0;
    int quarantine_target = 0;
    int quarantine_clean_run = 0;
    int64_t reseed_transition = -1;
    int64_t aligned_transition = -1;
    // The transition whose realized contact demoted this episode's owner out
    // of speculation. The contact ITSELF stays with the caller that sampled
    // it, because a peer compares a witness fingerprint and never the detail.
    int64_t breach_transition = -1;
    bool transport_decided = false;
    bool dissipate_decided = false;
    bool demoted = false;

    int next_id = 1;
    int next_write_id = 1;
    int last_closed_id = 0;
    int64_t last_closed_transition = -1;
    Attribution last_closure_attribution = Attribution::UNKNOWN;
    LocalVector<int> last_reopen_chain;
    int fallback_flap_level = 0;
    bool last_closure_was_fallback = false;
    bool flap_suppress_once = false;
    bool active = false;

    Episode() = default;
    Episode(const Episode &p_other);
    Episode &operator=(const Episode &p_other);

    void clear_active();
    void open(
        const Journal &p_journal,
        int64_t p_transition,
        Attribution p_attribution
    );
    void record_divergence(const Journal &p_journal, int64_t p_transition);
    void record_comparison(
        int64_t p_transition,
        int p_meter,
        bool p_agrees,
        int p_ack_age
    );
    int record_write(
        Operator p_operator,
        int64_t p_basis,
        int32_t p_delta_fp,
        const StringName &p_target,
        int p_ack_age,
        TriggerShape p_trigger_shape,
        bool p_evidence_free = false,
        bool p_null_operator = false
    );
    void stamp_write_delta(int32_t p_delta_fp);
    void record_decision(const EpisodeDecision &p_decision);
    bool operator_decided(Operator p_operator) const;

    bool count_non_contraction(TriggerShape p_shape, bool p_evidence_free);
    void record_escalation(TriggerShape p_live_shape);
    bool budget_exhausted() const;

    // Whether the NEWEST write naming p_operator is still awaiting a verdict.
    // Newest rather than any, because an operator retried after a refusal is
    // pending on its retry and not on what it already answered for.
    bool operator_pending(Operator p_operator) const;
    void record_breach(int64_t p_transition);
    void retire_agreement_run();
    void repin_generator(
        const Journal &p_journal,
        int64_t p_transition,
        Attribution p_attribution
    );
    void enter_fallback(int64_t p_transition, bool p_demoted = false);
    void close_fallback(int64_t p_transition);
    void suppress_next_flap();
    int flap_multiplier() const;
};

} // namespace netw::predict
