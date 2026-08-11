#include "netw/predict/episode_report.hpp"

#include "godot/class_db.hpp"

namespace netw {

namespace {

template <typename T> const T *at(const LocalVector<T> &p_values, int p_index) {
    return p_index >= 0 && p_index < int(p_values.size())
        ? &p_values[uint32_t(p_index)]
        : nullptr;
}

} // namespace

bool NetwPredictEpisodeReport::active() const {
    return episode.active;
}

int NetwPredictEpisodeReport::id() const {
    return episode.id;
}

int NetwPredictEpisodeReport::state() const {
    return int(episode.state);
}

int64_t NetwPredictEpisodeReport::opened_transition() const {
    return episode.opened_transition;
}

int64_t NetwPredictEpisodeReport::closed_transition() const {
    return episode.closed_transition;
}

int64_t NetwPredictEpisodeReport::fallback_transition() const {
    return episode.fallback_transition;
}

int NetwPredictEpisodeReport::resume_ack_age() const {
    return episode.resume_ack_age;
}

int NetwPredictEpisodeReport::quarantine_target() const {
    return episode.quarantine_target;
}

int NetwPredictEpisodeReport::quarantine_clean_run() const {
    return episode.quarantine_clean_run;
}

int64_t NetwPredictEpisodeReport::reseed_transition() const {
    return episode.reseed_transition;
}

int64_t NetwPredictEpisodeReport::aligned_transition() const {
    return episode.aligned_transition;
}

int64_t NetwPredictEpisodeReport::breach_transition() const {
    return episode.breach_transition;
}

int NetwPredictEpisodeReport::attribution() const {
    return int(episode.attribution);
}

int NetwPredictEpisodeReport::reopened_from() const {
    return episode.reopened_from;
}

int NetwPredictEpisodeReport::evidence_dropped() const {
    return episode.evidence_dropped;
}

int NetwPredictEpisodeReport::non_contraction_used() const {
    return episode.non_contraction_used;
}

int NetwPredictEpisodeReport::withheld_non_contractions() const {
    return episode.withheld_non_contractions;
}

int NetwPredictEpisodeReport::evidence_free_non_contractions() const {
    return episode.evidence_free_non_contractions;
}

int NetwPredictEpisodeReport::no_trigger_non_contractions() const {
    return episode.nc_no_trigger;
}

int NetwPredictEpisodeReport::mixed_trigger_non_contractions() const {
    return episode.nc_mixed_trigger;
}

int NetwPredictEpisodeReport::closure_used() const {
    return episode.closure_used;
}

int NetwPredictEpisodeReport::agreement_run() const {
    return episode.agreement_run;
}

int64_t NetwPredictEpisodeReport::last_comparison_transition() const {
    return episode.last_comparison_transition;
}

int NetwPredictEpisodeReport::agreement_write_id() const {
    return episode.agreement_write_id;
}

int NetwPredictEpisodeReport::last_write_id() const {
    return episode.last_write_id;
}

bool NetwPredictEpisodeReport::transport_decided() const {
    return episode.transport_decided;
}

bool NetwPredictEpisodeReport::dissipate_decided() const {
    return episode.dissipate_decided;
}

bool NetwPredictEpisodeReport::demoted() const {
    return episode.demoted;
}

bool NetwPredictEpisodeReport::generator_present() const {
    return episode.generator.present;
}

bool NetwPredictEpisodeReport::generator_beyond_retention() const {
    return episode.generator.beyond_retention;
}

int64_t NetwPredictEpisodeReport::generator_transition() const {
    return episode.generator.transition;
}

int64_t NetwPredictEpisodeReport::generator_label() const {
    return episode.generator.label;
}

int NetwPredictEpisodeReport::generator_domain() const {
    return int(episode.generator.domain);
}

int NetwPredictEpisodeReport::generator_attribution() const {
    return int(episode.generator.attribution);
}

int NetwPredictEpisodeReport::generator_flags() const {
    return episode.generator.flags;
}

int64_t NetwPredictEpisodeReport::generator_pre_fp() const {
    return episode.generator.evidence.pre_fp;
}

int64_t NetwPredictEpisodeReport::generator_c_hash() const {
    return episode.generator.evidence.c_hash;
}

int64_t NetwPredictEpisodeReport::generator_e_digest() const {
    return episode.generator.evidence.e_digest;
}

int64_t NetwPredictEpisodeReport::generator_post_fp() const {
    return episode.generator.evidence.post_fp;
}

int64_t NetwPredictEpisodeReport::generator_pre_pose_fp() const {
    return episode.generator.evidence.pre_families.pose;
}

int64_t NetwPredictEpisodeReport::generator_pre_momentum_fp() const {
    return episode.generator.evidence.pre_families.momentum;
}

int64_t NetwPredictEpisodeReport::generator_pre_controller_fp() const {
    return episode.generator.evidence.pre_families.controller;
}

int64_t NetwPredictEpisodeReport::generator_post_pose_fp() const {
    return episode.generator.evidence.post_families.pose;
}

int64_t NetwPredictEpisodeReport::generator_post_momentum_fp() const {
    return episode.generator.evidence.post_families.momentum;
}

int64_t NetwPredictEpisodeReport::generator_post_controller_fp() const {
    return episode.generator.evidence.post_families.controller;
}

int64_t NetwPredictEpisodeReport::generator_topology_fp() const {
    return episode.generator.evidence.topo_fp;
}

int64_t NetwPredictEpisodeReport::generator_witness_fp() const {
    return episode.generator.evidence.witness_fp;
}

int64_t NetwPredictEpisodeReport::generator_raw_fp() const {
    return episode.generator.evidence.raw_fp;
}

int NetwPredictEpisodeReport::generator_evidence_mask() const {
    return episode.generator.evidence.evidence_mask;
}

int NetwPredictEpisodeReport::reopen_count() const {
    return int(episode.reopen_chain.size());
}

int NetwPredictEpisodeReport::reopen_at(int p_index) const {
    const int *value = at(episode.reopen_chain, p_index);
    return value != nullptr ? *value : 0;
}

int NetwPredictEpisodeReport::taint_count() const {
    return int(episode.taint.size());
}

int64_t NetwPredictEpisodeReport::taint_at(int p_index) const {
    const int64_t *value = at(episode.taint, p_index);
    return value != nullptr ? *value : -1;
}

int NetwPredictEpisodeReport::secondary_generator_count() const {
    return int(episode.secondary_generators.size());
}

int64_t NetwPredictEpisodeReport::secondary_generator_transition(
    int p_index
) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->transition : -1;
}

int64_t NetwPredictEpisodeReport::secondary_generator_label(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->label : -1;
}

int NetwPredictEpisodeReport::secondary_generator_domain(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? int(row->domain) : 0;
}

int NetwPredictEpisodeReport::secondary_generator_attribution(
    int p_index
) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? int(row->attribution) : 0;
}

int NetwPredictEpisodeReport::secondary_generator_flags(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->flags : 0;
}

bool NetwPredictEpisodeReport::secondary_generator_beyond_retention(
    int p_index
) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr && row->beyond_retention;
}

int NetwPredictEpisodeReport::comparison_count() const {
    return int(episode.comparisons.size());
}

int64_t NetwPredictEpisodeReport::comparison_transition(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->transition : -1;
}

int NetwPredictEpisodeReport::comparison_meter(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->meter : 0;
}

int NetwPredictEpisodeReport::comparison_write_id(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->write_id : 0;
}

bool NetwPredictEpisodeReport::comparison_agrees(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr && row->agrees;
}

int NetwPredictEpisodeReport::write_count() const {
    return int(episode.writes.size());
}

int NetwPredictEpisodeReport::write_episode(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->episode : 0;
}

int NetwPredictEpisodeReport::write_id(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->write_id : 0;
}

int NetwPredictEpisodeReport::write_operator(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->op) : 0;
}

int64_t NetwPredictEpisodeReport::write_basis(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->basis : -1;
}

int64_t NetwPredictEpisodeReport::write_delta_fp(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->delta_fp : 0;
}

StringName NetwPredictEpisodeReport::write_target(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->target : StringName();
}

int NetwPredictEpisodeReport::write_outcome(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->outcome) : -1;
}

int NetwPredictEpisodeReport::write_meter_before(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->meter_before : 0;
}

int NetwPredictEpisodeReport::write_meter_after(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->meter_after : predict::METER_UNMEASURED;
}

int NetwPredictEpisodeReport::write_verdicts(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->verdicts : 0;
}

int NetwPredictEpisodeReport::write_verify_run(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->verify_run : 0;
}

int64_t NetwPredictEpisodeReport::write_judged_transition(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->judged_transition : -1;
}

int NetwPredictEpisodeReport::write_best_meter(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->best_meter : 0;
}

int NetwPredictEpisodeReport::write_trigger_shape(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->trigger_shape) : 0;
}

bool NetwPredictEpisodeReport::write_evidence_free(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr && row->evidence_free;
}

bool NetwPredictEpisodeReport::write_null_operator(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr && row->null_operator;
}

int NetwPredictEpisodeReport::decision_count() const {
    return int(episode.decisions.size());
}

int NetwPredictEpisodeReport::decision_operator(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? int(row->op) : 0;
}

int64_t NetwPredictEpisodeReport::decision_basis(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? row->basis : -1;
}

bool NetwPredictEpisodeReport::decision_eligible(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr && row->eligible;
}

bool NetwPredictEpisodeReport::decision_applied(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr && row->applied;
}

Dictionary NetwPredictEpisodeReport::decision_eligibility(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? row->eligibility.duplicate(true) : Dictionary();
}

#define NETW_BIND_EPISODE(m_name) \
    ClassDB::bind_method(D_METHOD(#m_name), &NetwPredictEpisodeReport::m_name)

#define NETW_BIND_EPISODE_AT(m_name) \
    ClassDB::bind_method( \
        D_METHOD(#m_name, "index"), \
        &NetwPredictEpisodeReport::m_name \
    )

void NetwPredictEpisodeReport::_bind_methods() {
    NETW_BIND_EPISODE(active);
    NETW_BIND_EPISODE(id);
    NETW_BIND_EPISODE(state);
    NETW_BIND_EPISODE(opened_transition);
    NETW_BIND_EPISODE(closed_transition);
    NETW_BIND_EPISODE(fallback_transition);
    NETW_BIND_EPISODE(resume_ack_age);
    NETW_BIND_EPISODE(quarantine_target);
    NETW_BIND_EPISODE(quarantine_clean_run);
    NETW_BIND_EPISODE(reseed_transition);
    NETW_BIND_EPISODE(aligned_transition);
    NETW_BIND_EPISODE(breach_transition);
    NETW_BIND_EPISODE(attribution);
    NETW_BIND_EPISODE(reopened_from);
    NETW_BIND_EPISODE(evidence_dropped);
    NETW_BIND_EPISODE(non_contraction_used);
    NETW_BIND_EPISODE(withheld_non_contractions);
    NETW_BIND_EPISODE(evidence_free_non_contractions);
    NETW_BIND_EPISODE(no_trigger_non_contractions);
    NETW_BIND_EPISODE(mixed_trigger_non_contractions);
    NETW_BIND_EPISODE(closure_used);
    NETW_BIND_EPISODE(agreement_run);
    NETW_BIND_EPISODE(last_comparison_transition);
    NETW_BIND_EPISODE(agreement_write_id);
    NETW_BIND_EPISODE(last_write_id);
    NETW_BIND_EPISODE(transport_decided);
    NETW_BIND_EPISODE(dissipate_decided);
    NETW_BIND_EPISODE(demoted);
    NETW_BIND_EPISODE(generator_present);
    NETW_BIND_EPISODE(generator_beyond_retention);
    NETW_BIND_EPISODE(generator_transition);
    NETW_BIND_EPISODE(generator_label);
    NETW_BIND_EPISODE(generator_domain);
    NETW_BIND_EPISODE(generator_attribution);
    NETW_BIND_EPISODE(generator_flags);
    NETW_BIND_EPISODE(generator_pre_fp);
    NETW_BIND_EPISODE(generator_c_hash);
    NETW_BIND_EPISODE(generator_e_digest);
    NETW_BIND_EPISODE(generator_post_fp);
    NETW_BIND_EPISODE(generator_pre_pose_fp);
    NETW_BIND_EPISODE(generator_pre_momentum_fp);
    NETW_BIND_EPISODE(generator_pre_controller_fp);
    NETW_BIND_EPISODE(generator_post_pose_fp);
    NETW_BIND_EPISODE(generator_post_momentum_fp);
    NETW_BIND_EPISODE(generator_post_controller_fp);
    NETW_BIND_EPISODE(generator_topology_fp);
    NETW_BIND_EPISODE(generator_witness_fp);
    NETW_BIND_EPISODE(generator_raw_fp);
    NETW_BIND_EPISODE(generator_evidence_mask);
    NETW_BIND_EPISODE(reopen_count);
    NETW_BIND_EPISODE(taint_count);
    NETW_BIND_EPISODE(secondary_generator_count);
    NETW_BIND_EPISODE(comparison_count);
    NETW_BIND_EPISODE(write_count);
    NETW_BIND_EPISODE(decision_count);
    NETW_BIND_EPISODE_AT(reopen_at);
    NETW_BIND_EPISODE_AT(taint_at);
    NETW_BIND_EPISODE_AT(secondary_generator_transition);
    NETW_BIND_EPISODE_AT(secondary_generator_label);
    NETW_BIND_EPISODE_AT(secondary_generator_domain);
    NETW_BIND_EPISODE_AT(secondary_generator_attribution);
    NETW_BIND_EPISODE_AT(secondary_generator_flags);
    NETW_BIND_EPISODE_AT(secondary_generator_beyond_retention);
    NETW_BIND_EPISODE_AT(comparison_transition);
    NETW_BIND_EPISODE_AT(comparison_meter);
    NETW_BIND_EPISODE_AT(comparison_write_id);
    NETW_BIND_EPISODE_AT(comparison_agrees);
    NETW_BIND_EPISODE_AT(write_episode);
    NETW_BIND_EPISODE_AT(write_id);
    NETW_BIND_EPISODE_AT(write_operator);
    NETW_BIND_EPISODE_AT(write_basis);
    NETW_BIND_EPISODE_AT(write_delta_fp);
    NETW_BIND_EPISODE_AT(write_target);
    NETW_BIND_EPISODE_AT(write_outcome);
    NETW_BIND_EPISODE_AT(write_meter_before);
    NETW_BIND_EPISODE_AT(write_meter_after);
    NETW_BIND_EPISODE_AT(write_verdicts);
    NETW_BIND_EPISODE_AT(write_verify_run);
    NETW_BIND_EPISODE_AT(write_judged_transition);
    NETW_BIND_EPISODE_AT(write_best_meter);
    NETW_BIND_EPISODE_AT(write_trigger_shape);
    NETW_BIND_EPISODE_AT(write_evidence_free);
    NETW_BIND_EPISODE_AT(write_null_operator);
    NETW_BIND_EPISODE_AT(decision_operator);
    NETW_BIND_EPISODE_AT(decision_basis);
    NETW_BIND_EPISODE_AT(decision_eligible);
    NETW_BIND_EPISODE_AT(decision_applied);
    NETW_BIND_EPISODE_AT(decision_eligibility);

    BIND_ENUM_CONSTANT(EPISODE_OPEN);
    BIND_ENUM_CONSTANT(EPISODE_CLOSED);
    BIND_ENUM_CONSTANT(EPISODE_FALLBACK);
}

#undef NETW_BIND_EPISODE_AT
#undef NETW_BIND_EPISODE

} // namespace netw
