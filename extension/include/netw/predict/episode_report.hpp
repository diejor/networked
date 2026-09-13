#pragma once

#include "godot/variant.hpp"
#include "netw/predict/episode.hpp"

namespace netw {

struct EpisodeReport {
    predict::Episode episode;

    godot::Dictionary to_dictionary(
        const godot::Dictionary &p_witness_details
    ) const;

    bool active() const;
    int id() const;
    int state() const;
    int64_t opened_transition() const;
    int64_t closed_transition() const;
    int64_t fallback_transition() const;
    int resume_ack_age() const;
    int quarantine_target() const;
    int quarantine_clean_run() const;
    int64_t reseed_transition() const;
    int64_t aligned_transition() const;
    int64_t breach_transition() const;
    int attribution() const;
    int reopened_from() const;
    int evidence_dropped() const;
    int non_contraction_used() const;
    int withheld_non_contractions() const;
    int evidence_free_non_contractions() const;
    int no_trigger_non_contractions() const;
    int mixed_trigger_non_contractions() const;
    int closure_used() const;
    int agreement_run() const;
    int64_t last_comparison_transition() const;
    int agreement_write_id() const;
    int last_write_id() const;
    bool transport_decided() const;
    bool dissipate_decided() const;
    bool demoted() const;

    bool generator_present() const;
    bool generator_beyond_retention() const;
    int64_t generator_transition() const;
    int64_t generator_label() const;
    int generator_domain() const;
    int generator_attribution() const;
    int generator_flags() const;
    int64_t generator_pre_fp() const;
    int64_t generator_c_hash() const;
    int64_t generator_e_digest() const;
    int64_t generator_post_fp() const;
    int64_t generator_pre_pose_fp() const;
    int64_t generator_pre_momentum_fp() const;
    int64_t generator_pre_controller_fp() const;
    int64_t generator_post_pose_fp() const;
    int64_t generator_post_momentum_fp() const;
    int64_t generator_post_controller_fp() const;
    int64_t generator_topology_fp() const;
    int64_t generator_witness_fp() const;
    int64_t generator_raw_fp() const;
    int generator_evidence_mask() const;

    int reopen_count() const;
    int reopen_at(int p_index) const;
    int taint_count() const;
    int64_t taint_at(int p_index) const;

    int secondary_generator_count() const;
    int64_t secondary_generator_transition(int p_index) const;
    int64_t secondary_generator_label(int p_index) const;
    int secondary_generator_domain(int p_index) const;
    int secondary_generator_attribution(int p_index) const;
    int secondary_generator_flags(int p_index) const;
    bool secondary_generator_beyond_retention(int p_index) const;

    int comparison_count() const;
    int64_t comparison_transition(int p_index) const;
    int comparison_meter(int p_index) const;
    int comparison_write_id(int p_index) const;
    bool comparison_agrees(int p_index) const;

    int write_count() const;
    int write_episode(int p_index) const;
    int write_id(int p_index) const;
    int write_operator(int p_index) const;
    int64_t write_basis(int p_index) const;
    int64_t write_delta_fp(int p_index) const;
    godot::StringName write_target(int p_index) const;
    int write_outcome(int p_index) const;
    int write_meter_before(int p_index) const;
    int write_meter_after(int p_index) const;
    int write_verdicts(int p_index) const;
    int write_verify_run(int p_index) const;
    int64_t write_judged_transition(int p_index) const;
    int write_best_meter(int p_index) const;
    int write_trigger_shape(int p_index) const;
    bool write_evidence_free(int p_index) const;
    bool write_null_operator(int p_index) const;

    int decision_count() const;
    int decision_operator(int p_index) const;
    int64_t decision_basis(int p_index) const;
    bool decision_eligible(int p_index) const;
    bool decision_applied(int p_index) const;
    godot::Dictionary decision_eligibility(int p_index) const;
};

} // namespace netw
