#include "netw/predict/episode_report.hpp"

using namespace godot;

namespace netw {

namespace {

template <typename T> const T *at(const LocalVector<T> &p_values, int p_index) {
    return p_index >= 0 && p_index < int(p_values.size())
        ? &p_values[uint32_t(p_index)]
        : nullptr;
}

} // namespace

bool EpisodeReport::active() const {
    return episode.active;
}

int EpisodeReport::id() const {
    return episode.id;
}

int EpisodeReport::state() const {
    return int(episode.state);
}

int64_t EpisodeReport::opened_transition() const {
    return episode.opened_transition;
}

int64_t EpisodeReport::closed_transition() const {
    return episode.closed_transition;
}

int64_t EpisodeReport::fallback_transition() const {
    return episode.fallback_transition;
}

int EpisodeReport::resume_ack_age() const {
    return episode.resume_ack_age;
}

int EpisodeReport::quarantine_target() const {
    return episode.quarantine_target;
}

int EpisodeReport::quarantine_clean_run() const {
    return episode.quarantine_clean_run;
}

int64_t EpisodeReport::reseed_transition() const {
    return episode.reseed_transition;
}

int64_t EpisodeReport::aligned_transition() const {
    return episode.aligned_transition;
}

int64_t EpisodeReport::breach_transition() const {
    return episode.breach_transition;
}

int EpisodeReport::attribution() const {
    return int(episode.attribution);
}

int EpisodeReport::reopened_from() const {
    return episode.reopened_from;
}

int EpisodeReport::evidence_dropped() const {
    return episode.evidence_dropped;
}

int EpisodeReport::non_contraction_used() const {
    return episode.non_contraction_used;
}

int EpisodeReport::withheld_non_contractions() const {
    return episode.withheld_non_contractions;
}

int EpisodeReport::evidence_free_non_contractions() const {
    return episode.evidence_free_non_contractions;
}

int EpisodeReport::no_trigger_non_contractions() const {
    return episode.nc_no_trigger;
}

int EpisodeReport::mixed_trigger_non_contractions() const {
    return episode.nc_mixed_trigger;
}

int EpisodeReport::closure_used() const {
    return episode.closure_used;
}

int EpisodeReport::agreement_run() const {
    return episode.agreement_run;
}

int64_t EpisodeReport::last_comparison_transition() const {
    return episode.last_comparison_transition;
}

int EpisodeReport::agreement_write_id() const {
    return episode.agreement_write_id;
}

int EpisodeReport::last_write_id() const {
    return episode.last_write_id;
}

bool EpisodeReport::transport_decided() const {
    return episode.transport_decided;
}

bool EpisodeReport::dissipate_decided() const {
    return episode.dissipate_decided;
}

bool EpisodeReport::demoted() const {
    return episode.demoted;
}

bool EpisodeReport::generator_present() const {
    return episode.generator.present;
}

bool EpisodeReport::generator_beyond_retention() const {
    return episode.generator.beyond_retention;
}

int64_t EpisodeReport::generator_transition() const {
    return episode.generator.transition;
}

int64_t EpisodeReport::generator_label() const {
    return episode.generator.label;
}

int EpisodeReport::generator_domain() const {
    return int(episode.generator.domain);
}

int EpisodeReport::generator_attribution() const {
    return int(episode.generator.attribution);
}

int EpisodeReport::generator_flags() const {
    return episode.generator.flags;
}

int64_t EpisodeReport::generator_pre_fp() const {
    return episode.generator.evidence.pre_fp;
}

int64_t EpisodeReport::generator_c_hash() const {
    return episode.generator.evidence.c_hash;
}

int64_t EpisodeReport::generator_e_digest() const {
    return episode.generator.evidence.e_digest;
}

int64_t EpisodeReport::generator_post_fp() const {
    return episode.generator.evidence.post_fp;
}

int64_t EpisodeReport::generator_pre_pose_fp() const {
    return episode.generator.evidence.pre_families.pose;
}

int64_t EpisodeReport::generator_pre_momentum_fp() const {
    return episode.generator.evidence.pre_families.momentum;
}

int64_t EpisodeReport::generator_pre_controller_fp() const {
    return episode.generator.evidence.pre_families.controller;
}

int64_t EpisodeReport::generator_post_pose_fp() const {
    return episode.generator.evidence.post_families.pose;
}

int64_t EpisodeReport::generator_post_momentum_fp() const {
    return episode.generator.evidence.post_families.momentum;
}

int64_t EpisodeReport::generator_post_controller_fp() const {
    return episode.generator.evidence.post_families.controller;
}

int64_t EpisodeReport::generator_topology_fp() const {
    return episode.generator.evidence.topo_fp;
}

int64_t EpisodeReport::generator_witness_fp() const {
    return episode.generator.evidence.witness_fp;
}

int64_t EpisodeReport::generator_raw_fp() const {
    return episode.generator.evidence.raw_fp;
}

int EpisodeReport::generator_evidence_mask() const {
    return episode.generator.evidence.evidence_mask;
}

int EpisodeReport::reopen_count() const {
    return int(episode.reopen_chain.size());
}

int EpisodeReport::reopen_at(int p_index) const {
    const int *value = at(episode.reopen_chain, p_index);
    return value != nullptr ? *value : 0;
}

int EpisodeReport::taint_count() const {
    return int(episode.taint.size());
}

int64_t EpisodeReport::taint_at(int p_index) const {
    const int64_t *value = at(episode.taint, p_index);
    return value != nullptr ? *value : -1;
}

int EpisodeReport::secondary_generator_count() const {
    return int(episode.secondary_generators.size());
}

int64_t EpisodeReport::secondary_generator_transition(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->transition : -1;
}

int64_t EpisodeReport::secondary_generator_label(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->label : -1;
}

int EpisodeReport::secondary_generator_domain(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? int(row->domain) : 0;
}

int EpisodeReport::secondary_generator_attribution(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? int(row->attribution) : 0;
}

int EpisodeReport::secondary_generator_flags(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr ? row->flags : 0;
}

bool EpisodeReport::secondary_generator_beyond_retention(int p_index) const {
    const predict::PinnedRow *row = at(episode.secondary_generators, p_index);
    return row != nullptr && row->beyond_retention;
}

int EpisodeReport::comparison_count() const {
    return int(episode.comparisons.size());
}

int64_t EpisodeReport::comparison_transition(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->transition : -1;
}

int EpisodeReport::comparison_meter(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->meter : 0;
}

int EpisodeReport::comparison_write_id(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr ? row->write_id : 0;
}

bool EpisodeReport::comparison_agrees(int p_index) const {
    const predict::EpisodeComparison *row = at(episode.comparisons, p_index);
    return row != nullptr && row->agrees;
}

int EpisodeReport::write_count() const {
    return int(episode.writes.size());
}

int EpisodeReport::write_episode(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->episode : 0;
}

int EpisodeReport::write_id(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->write_id : 0;
}

int EpisodeReport::write_operator(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->op) : 0;
}

int64_t EpisodeReport::write_basis(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->basis : -1;
}

int64_t EpisodeReport::write_delta_fp(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->delta_fp : 0;
}

StringName EpisodeReport::write_target(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->target : StringName();
}

int EpisodeReport::write_outcome(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->outcome) : -1;
}

int EpisodeReport::write_meter_before(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->meter_before : 0;
}

int EpisodeReport::write_meter_after(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->meter_after : predict::METER_UNMEASURED;
}

int EpisodeReport::write_verdicts(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->verdicts : 0;
}

int EpisodeReport::write_verify_run(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->verify_run : 0;
}

int64_t EpisodeReport::write_judged_transition(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->judged_transition : -1;
}

int EpisodeReport::write_best_meter(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? row->best_meter : 0;
}

int EpisodeReport::write_trigger_shape(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr ? int(row->trigger_shape) : 0;
}

bool EpisodeReport::write_evidence_free(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr && row->evidence_free;
}

bool EpisodeReport::write_null_operator(int p_index) const {
    const predict::EpisodeWrite *row = at(episode.writes, p_index);
    return row != nullptr && row->null_operator;
}

int EpisodeReport::decision_count() const {
    return int(episode.decisions.size());
}

int EpisodeReport::decision_operator(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? int(row->op) : 0;
}

int64_t EpisodeReport::decision_basis(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? row->basis : -1;
}

bool EpisodeReport::decision_eligible(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr && row->eligible;
}

bool EpisodeReport::decision_applied(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr && row->applied;
}

Dictionary EpisodeReport::decision_eligibility(int p_index) const {
    const predict::EpisodeDecision *row = at(episode.decisions, p_index);
    return row != nullptr ? row->eligibility.duplicate(true) : Dictionary();
}

Dictionary EpisodeReport::to_dictionary(
    const Dictionary &p_witness_details
) const {
    Dictionary out;
    if (!active()) {
        return out;
    }
    Array writes;
    for (int at = 0; at < write_count(); ++at) {
        Dictionary row;
        row["episode"] = write_episode(at);
        row["write_id"] = write_id(at);
        row["operator"] = write_operator(at);
        row["basis"] = write_basis(at);
        row["delta_fp"] = write_delta_fp(at);
        row["target"] = write_target(at);
        row["outcome"] = write_outcome(at);
        row["meter_before"] = write_meter_before(at);
        row["meter_after"] = write_meter_after(at);
        row["verdicts"] = write_verdicts(at);
        row["verify_run"] = write_verify_run(at);
        row["judged_transition"] = write_judged_transition(at);
        row["best_meter"] = write_best_meter(at);
        row["trigger_shape"] = write_trigger_shape(at);
        row["evidence_free"] = write_evidence_free(at);
        row["null_operator"] = write_null_operator(at);
        writes.push_back(row);
    }
    Array comparisons;
    for (int at = 0; at < comparison_count(); ++at) {
        Dictionary row;
        row["transition"] = comparison_transition(at);
        row["meter"] = comparison_meter(at);
        row["agrees"] = comparison_agrees(at);
        row["write_id"] = comparison_write_id(at);
        comparisons.push_back(row);
    }
    Array decisions;
    for (int at = 0; at < decision_count(); ++at) {
        Dictionary row;
        row["operator"] = decision_operator(at);
        row["basis"] = decision_basis(at);
        row["eligible"] = decision_eligible(at);
        row["applied"] = decision_applied(at);
        row["eligibility"] = decision_eligibility(at);
        decisions.push_back(row);
    }
    Array taint;
    for (int at = 0; at < taint_count(); ++at) {
        taint.push_back(taint_at(at));
    }
    Array reopen_chain;
    for (int at = 0; at < reopen_count(); ++at) {
        reopen_chain.push_back(reopen_at(at));
    }
    Array secondary;
    for (int at = 0; at < secondary_generator_count(); ++at) {
        Dictionary row;
        row["transition"] = secondary_generator_transition(at);
        row["label"] = secondary_generator_label(at);
        row["domain"] = secondary_generator_domain(at);
        row["attribution"] = secondary_generator_attribution(at);
        row["flags"] = secondary_generator_flags(at);
        row["beyond_retention"] = secondary_generator_beyond_retention(at);
        secondary.push_back(row);
    }
    Dictionary generator;
    if (!generator_present()) {
        generator["status"] = StringName("UNKNOWN_BEYOND_RETENTION");
    } else {
        generator["transition"] = generator_transition();
        generator["label"] = generator_label();
        generator["c_hash"] = generator_c_hash();
        generator["e_digest"] = generator_e_digest();
        generator["pre_fp"] = generator_pre_fp();
        generator["topo_fp"] = generator_topology_fp();
        generator["raw_fp"] = generator_raw_fp();
        generator["witness_fp"] = generator_witness_fp();
        generator["evidence_mask"] = generator_evidence_mask();
        generator["pre_pose_fp"] = generator_pre_pose_fp();
        generator["pre_momentum_fp"] = generator_pre_momentum_fp();
        generator["pre_controller_fp"] = generator_pre_controller_fp();
        generator["post_fp"] = generator_post_fp();
        generator["post_pose_fp"] = generator_post_pose_fp();
        generator["post_momentum_fp"] = generator_post_momentum_fp();
        generator["post_controller_fp"] = generator_post_controller_fp();
        generator["domain"] = generator_domain();
        generator["attribution"] = generator_attribution();
        generator["flags"] = generator_flags();
        generator["beyond_retention"] = generator_beyond_retention();
        generator["witness_detail"]
            = p_witness_details.get(generator_transition(), Dictionary());
    }

    out["id"] = id();
    out["opened_transition"] = opened_transition();
    out["generator_row_copy"] = generator;
    out["attribution"] = attribution();
    out["writes"] = writes;
    out["comparisons"] = comparisons;
    out["decisions"] = decisions;
    out["taint"] = taint;
    out["secondary_generators"] = secondary;
    out["reopen_chain"] = reopen_chain;
    out["reopened_from"] = reopened_from();
    out["state"] = state();
    out["non_contraction_used"] = non_contraction_used();
    out["withheld_non_contractions"] = withheld_non_contractions();
    out["evidence_free_non_contractions"] = evidence_free_non_contractions();
    out["nc_no_trigger"] = no_trigger_non_contractions();
    out["nc_mixed_trigger"] = mixed_trigger_non_contractions();
    out["closure_used"] = closure_used();
    out["agreement_run"] = agreement_run();
    out["last_comparison_transition"] = last_comparison_transition();
    out["agreement_write_id"] = agreement_write_id();
    out["last_write_id"] = last_write_id();
    out["closed_transition"] = closed_transition();
    out["fallback_transition"] = fallback_transition();
    out["resume_ack_age"] = resume_ack_age();
    out["quarantine_target"] = quarantine_target();
    out["quarantine_clean_run"] = quarantine_clean_run();
    out["reseed_transition"] = reseed_transition();
    out["aligned_transition"] = aligned_transition();
    out["evidence_dropped"] = evidence_dropped();
    out["transport_decided"] = transport_decided();
    out["dissipate_decided"] = dissipate_decided();
    out["demoted"] = demoted();
    out["breach_transition"] = breach_transition();
    const Dictionary breach_witness
        = p_witness_details.get(breach_transition(), Dictionary());
    out["breach_witness"] = breach_witness;

    Dictionary named_generator;
    named_generator["transition"]
        = generator_present() ? generator_transition() : opened_transition();
    named_generator["boundary"] = attribution();
    named_generator["row"] = generator;
    out["generator"] = named_generator;

    Array attempts;
    Dictionary used;
    for (int at = 0; at < decisions.size(); ++at) {
        Dictionary decision = Dictionary(decisions[at]).duplicate(true);
        Dictionary matched;
        for (int w = 0; w < writes.size(); ++w) {
            if (used.has(w)) {
                continue;
            }
            const Dictionary candidate = writes[w];
            if (int64_t(candidate["operator"]) == int64_t(decision["operator"])
                && int64_t(candidate["basis"]) == int64_t(decision["basis"])) {
                matched = candidate.duplicate(true);
                used[w] = true;
                break;
            }
        }
        decision["outcome"]
            = matched.is_empty() ? Variant(-1) : matched["outcome"];
        decision["write"] = matched;
        attempts.push_back(decision);
    }
    for (int w = 0; w < writes.size(); ++w) {
        if (used.has(w)) {
            continue;
        }
        const Dictionary write = Dictionary(writes[w]).duplicate(true);
        Dictionary row;
        row["operator"] = write["operator"];
        row["basis"] = write["basis"];
        row["eligible"] = true;
        row["applied"] = true;
        row["eligibility"] = Dictionary();
        row["outcome"] = write["outcome"];
        row["write"] = write;
        attempts.push_back(row);
    }
    out["operators"] = attempts;
    out["contraction"] = comparisons.duplicate(true);

    Dictionary disposition;
    disposition["state"] = state();
    disposition["non_contraction_used"] = non_contraction_used();
    disposition["withheld_non_contractions"] = withheld_non_contractions();
    disposition["evidence_free_non_contractions"]
        = evidence_free_non_contractions();
    disposition["nc_no_trigger"] = no_trigger_non_contractions();
    disposition["nc_mixed_trigger"] = mixed_trigger_non_contractions();
    disposition["closure_used"] = closure_used();
    disposition["closed_transition"] = closed_transition();
    disposition["fallback_transition"] = fallback_transition();
    disposition["demoted"] = demoted();
    disposition["breach_transition"] = breach_transition();
    disposition["breach_witness"] = breach_witness;
    disposition["resume_ack_age"] = resume_ack_age();
    disposition["quarantine_target"] = quarantine_target();
    disposition["quarantine_clean_run"] = quarantine_clean_run();
    disposition["reseed_transition"] = reseed_transition();
    disposition["aligned_transition"] = aligned_transition();
    disposition["evidence_dropped"] = evidence_dropped();
    out["disposition"] = disposition;

    if (reopen_chain.is_empty() && reopened_from() > 0) {
        reopen_chain.push_back(reopened_from());
        reopen_chain.push_back(id());
        out["reopen_chain"] = reopen_chain;
    }
    return out;
}

} // namespace netw
