#include "netw/predict/episode.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::predict {

namespace {

int logical_index(const Journal &p_journal, int64_t p_transition) {
    for (int at = 0; at < p_journal.size(); ++at) {
        if (p_journal.transition_at(at) == p_transition) {
            return at;
        }
    }
    return -1;
}

PinnedRow pin_row(const Journal &p_journal, int64_t p_transition) {
    PinnedRow out;
    const int at = logical_index(p_journal, p_transition);
    if (at < 0) {
        out.beyond_retention = true;
        return out;
    }
    out.transition = p_transition;
    out.label = p_journal.label_at(at);
    out.domain = Domain(p_journal.domain_at(at));
    out.attribution = Attribution(p_journal.attribution_at(at));
    out.flags = p_journal.flags_at(at);
    out.evidence = p_journal.evidence_of(p_transition);
    out.present = true;
    return out;
}

bool actionable(const PinnedRow &p_row) {
    return p_row.present && (p_row.flags & ROW_DIVERGENT) != 0
        && p_row.domain == Domain::OUT_OF_DOMAIN;
}

PinnedRow pin_generator(const Journal &p_journal, int64_t p_transition) {
    int at = logical_index(p_journal, p_transition);
    if (at < 0) {
        PinnedRow out;
        out.beyond_retention = true;
        return out;
    }
    PinnedRow row = pin_row(p_journal, p_transition);
    if (actionable(row)) {
        while (at > 0) {
            const PinnedRow previous
                = pin_row(p_journal, p_journal.transition_at(at - 1));
            if (!actionable(previous)) {
                break;
            }
            at -= 1;
            row = previous;
        }
    }
    if (at == 0 && p_journal.size() >= p_journal.capacity_limit()
        && actionable(row)) {
        row.beyond_retention = true;
    }
    return row;
}

template <typename T> void trim(LocalVector<T> &r_values, int &r_dropped) {
    while (int(r_values.size()) > EPISODE_EVIDENCE_LIMIT) {
        r_values.remove_at(0);
        r_dropped += 1;
    }
}

template <typename T>
void copy_values(LocalVector<T> &r_target, const LocalVector<T> &p_source) {
    r_target.resize(p_source.size());
    for (uint32_t at = 0; at < p_source.size(); ++at) {
        r_target[at] = p_source[at];
    }
}

int close_run(int p_ack_age) {
    return std::clamp(
        std::max(0, p_ack_age) * 2,
        EPISODE_CLOSE_RUN_MIN,
        EPISODE_CLOSE_RUN_MAX
    );
}

} // namespace

Episode::Episode(const Episode &p_other) {
    *this = p_other;
}

Episode &Episode::operator=(const Episode &p_other) {
    if (this == &p_other) {
        return *this;
    }
    generator = p_other.generator;
    copy_values(writes, p_other.writes);
    copy_values(taint, p_other.taint);
    copy_values(secondary_generators, p_other.secondary_generators);
    copy_values(comparisons, p_other.comparisons);
    copy_values(decisions, p_other.decisions);
    copy_values(reopen_chain, p_other.reopen_chain);
    id = p_other.id;
    opened_transition = p_other.opened_transition;
    attribution = p_other.attribution;
    state = p_other.state;
    reopened_from = p_other.reopened_from;
    evidence_dropped = p_other.evidence_dropped;
    non_contraction_used = p_other.non_contraction_used;
    withheld_non_contractions = p_other.withheld_non_contractions;
    evidence_free_non_contractions = p_other.evidence_free_non_contractions;
    nc_no_trigger = p_other.nc_no_trigger;
    nc_mixed_trigger = p_other.nc_mixed_trigger;
    closure_used = p_other.closure_used;
    agreement_run = p_other.agreement_run;
    last_comparison_transition = p_other.last_comparison_transition;
    agreement_write_id = p_other.agreement_write_id;
    last_write_id = p_other.last_write_id;
    closed_transition = p_other.closed_transition;
    fallback_transition = p_other.fallback_transition;
    resume_ack_age = p_other.resume_ack_age;
    quarantine_target = p_other.quarantine_target;
    quarantine_clean_run = p_other.quarantine_clean_run;
    reseed_transition = p_other.reseed_transition;
    aligned_transition = p_other.aligned_transition;
    breach_transition = p_other.breach_transition;
    transport_decided = p_other.transport_decided;
    dissipate_decided = p_other.dissipate_decided;
    demoted = p_other.demoted;
    next_id = p_other.next_id;
    next_write_id = p_other.next_write_id;
    last_closed_id = p_other.last_closed_id;
    last_closed_transition = p_other.last_closed_transition;
    last_closure_attribution = p_other.last_closure_attribution;
    copy_values(last_reopen_chain, p_other.last_reopen_chain);
    fallback_flap_level = p_other.fallback_flap_level;
    last_closure_was_fallback = p_other.last_closure_was_fallback;
    flap_suppress_once = p_other.flap_suppress_once;
    active = p_other.active;
    return *this;
}

void Episode::clear_active() {
    generator = PinnedRow();
    writes.clear();
    taint.clear();
    secondary_generators.clear();
    comparisons.clear();
    decisions.clear();
    reopen_chain.clear();
    id = 0;
    opened_transition = -1;
    attribution = Attribution::UNKNOWN;
    state = EpisodeState::OPEN;
    reopened_from = 0;
    evidence_dropped = 0;
    non_contraction_used = 0;
    withheld_non_contractions = 0;
    evidence_free_non_contractions = 0;
    nc_no_trigger = 0;
    nc_mixed_trigger = 0;
    closure_used = 0;
    agreement_run = 0;
    last_comparison_transition = -1;
    agreement_write_id = 0;
    last_write_id = 0;
    closed_transition = -1;
    fallback_transition = -1;
    resume_ack_age = 0;
    quarantine_target = 0;
    quarantine_clean_run = 0;
    reseed_transition = -1;
    aligned_transition = -1;
    breach_transition = -1;
    transport_decided = false;
    dissipate_decided = false;
    demoted = false;
    next_write_id = 1;
    active = false;
}

void Episode::open(
    const Journal &p_journal,
    int64_t p_transition,
    Attribution p_attribution
) {
    NETW_ZONE_NC("NetwPredict open episode", colors::PREDICTION);
    const int saved_next_id = next_id;
    const int saved_closed_id = last_closed_id;
    const int64_t saved_closed_transition = last_closed_transition;
    const Attribution saved_closure_attribution = last_closure_attribution;
    const int saved_flap_level = fallback_flap_level;
    const bool saved_closure_was_fallback = last_closure_was_fallback;
    const bool held = flap_suppress_once;
    LocalVector<int> saved_chain;
    copy_values(saved_chain, last_reopen_chain);
    clear_active();
    next_id = saved_next_id;
    last_closed_id = saved_closed_id;
    last_closed_transition = saved_closed_transition;
    last_closure_attribution = saved_closure_attribution;
    fallback_flap_level = saved_flap_level;
    last_closure_was_fallback = saved_closure_was_fallback;
    flap_suppress_once = false;
    copy_values(last_reopen_chain, saved_chain);

    const bool reopened = last_closed_id > 0
        && p_transition >= last_closed_transition
        && p_transition - last_closed_transition <= EPISODE_FLAP_WINDOW;
    if (reopened && last_closure_was_fallback
        && p_attribution != Attribution::TOPOLOGY
        && p_attribution != last_closure_attribution && !held) {
        fallback_flap_level += 1;
    } else if (!reopened) {
        fallback_flap_level = 0;
    }
    last_closure_attribution = p_attribution;
    reopened_from = reopened ? last_closed_id : 0;
    if (reopened) {
        copy_values(reopen_chain, last_reopen_chain);
    }
    if (reopened
        && (reopen_chain.is_empty()
            || reopen_chain[reopen_chain.size() - 1] != reopened_from)) {
        reopen_chain.push_back(reopened_from);
    }
    id = next_id;
    next_id += 1;
    reopen_chain.push_back(id);
    copy_values(last_reopen_chain, reopen_chain);
    opened_transition = p_transition;
    attribution = p_attribution;
    generator = pin_generator(p_journal, p_transition);
    state = EpisodeState::OPEN;
    active = true;
}

void Episode::record_divergence(
    const Journal &p_journal,
    int64_t p_transition
) {
    if (!active || state != EpisodeState::OPEN
        || p_transition == opened_transition) {
        return;
    }
    const PinnedRow row = pin_row(p_journal, p_transition);
    const bool independent = row.present
        && row.attribution != Attribution::UNKNOWN
        && row.attribution != Attribution::PRE_STATE;
    if (!independent) {
        for (uint32_t at = 0; at < taint.size(); ++at) {
            if (taint[at] == p_transition) {
                return;
            }
        }
        taint.push_back(p_transition);
        trim(taint, evidence_dropped);
        return;
    }
    for (uint32_t at = 0; at < secondary_generators.size(); ++at) {
        if (secondary_generators[at].transition == p_transition) {
            return;
        }
    }
    if (int(secondary_generators.size()) >= EPISODE_GENERATOR_LIMIT) {
        evidence_dropped += 1;
        return;
    }
    secondary_generators.push_back(row);
}

void Episode::record_comparison(
    int64_t p_transition,
    int p_meter,
    bool p_agrees,
    int p_ack_age
) {
    if (!active || state != EpisodeState::OPEN
        || p_transition <= last_comparison_transition) {
        return;
    }
    NETW_ZONE_NC("NetwPredict judge episode", colors::PREDICTION);
    for (uint32_t at = 0; at < writes.size(); ++at) {
        EpisodeWrite &write = writes[at];
        if (write.outcome != OperatorOutcome::PENDING
            || p_transition <= write.basis) {
            continue;
        }
        if (write.op == Operator::DISSIPATE) {
            if (p_meter == 0) {
                write.outcome = OperatorOutcome::CONTRACTED;
                write.judged_transition = p_transition;
                write.meter_after = p_meter;
            } else if (p_meter < write.best_meter) {
                write.best_meter = p_meter;
                write.verdicts = 0;
            } else {
                write.verdicts += 1;
            }
        } else if (p_meter == 0 || p_meter < write.meter_before) {
            write.outcome = OperatorOutcome::CONTRACTED;
            write.judged_transition = p_transition;
            write.meter_after = p_meter;
        } else {
            write.verdicts += 1;
        }
        if (write.outcome == OperatorOutcome::PENDING
            && write.verdicts >= write.verify_run) {
            const bool withheld = !write.evidence_free
                && write.trigger_shape == TriggerShape::ALL_WITHHELD;
            write.outcome = withheld ? OperatorOutcome::WITHHELD
                                     : OperatorOutcome::FAILED_TO_CONTRACT;
            write.judged_transition = p_transition;
            write.meter_after = p_meter;
            count_non_contraction(write.trigger_shape, write.evidence_free);
        }
    }

    EpisodeComparison comparison;
    comparison.transition = p_transition;
    comparison.meter = p_meter;
    comparison.agrees = p_agrees;
    comparison.write_id = last_write_id;
    comparisons.push_back(comparison);
    trim(comparisons, evidence_dropped);
    last_comparison_transition = p_transition;
    if (p_agrees) {
        agreement_run
            = agreement_write_id == last_write_id ? agreement_run + 1 : 1;
        agreement_write_id = last_write_id;
    } else {
        agreement_run = 0;
    }
    if (agreement_run >= close_run(p_ack_age)) {
        state = EpisodeState::CLOSED;
        closed_transition = p_transition;
        last_closed_id = id;
        last_closed_transition = p_transition;
        last_closure_attribution = attribution;
        last_closure_was_fallback = false;
        fallback_flap_level = 0;
    }
}

int Episode::record_write(
    Operator p_operator,
    int64_t p_basis,
    int32_t p_delta_fp,
    const StringName &p_target,
    int p_ack_age,
    TriggerShape p_trigger_shape,
    bool p_evidence_free,
    bool p_null_operator
) {
    if (!active
        || (state != EpisodeState::OPEN && state != EpisodeState::FALLBACK)) {
        return 0;
    }
    EpisodeWrite write;
    write.episode = id;
    write.write_id = next_write_id;
    next_write_id += 1;
    write.op = p_operator;
    write.basis = p_basis;
    write.delta_fp = p_delta_fp;
    write.target = p_target;
    write.verify_run = p_null_operator
        ? EPISODE_CLOSE_RUN_MAX
        : std::max(EPISODE_CLOSE_RUN_MIN, p_ack_age);
    write.evidence_free = p_evidence_free;
    write.null_operator = p_null_operator;
    write.trigger_shape = p_trigger_shape;
    if (!comparisons.is_empty()) {
        write.meter_before = comparisons[comparisons.size() - 1].meter;
        write.best_meter = write.meter_before;
    }
    writes.push_back(write);
    trim(writes, evidence_dropped);
    last_write_id = write.write_id;
    agreement_run = 0;
    if (p_operator == Operator::FULL_CLOSURE) {
        closure_used += 1;
    }
    return write.write_id;
}

void Episode::stamp_write_delta(int32_t p_delta_fp) {
    if (!writes.is_empty()) {
        writes[writes.size() - 1].delta_fp = p_delta_fp;
    }
}

void Episode::record_decision(const EpisodeDecision &p_decision) {
    if (!active) {
        return;
    }
    decisions.push_back(p_decision);
    trim(decisions, evidence_dropped);
    if (p_decision.op == Operator::TRANSPORT_DELTA) {
        transport_decided = true;
    } else if (p_decision.op == Operator::DISSIPATE) {
        dissipate_decided = true;
    }
}

bool Episode::operator_decided(Operator p_operator) const {
    if (p_operator == Operator::TRANSPORT_DELTA) {
        return transport_decided;
    }
    return p_operator == Operator::DISSIPATE && dissipate_decided;
}

bool Episode::count_non_contraction(
    TriggerShape p_shape,
    bool p_evidence_free
) {
    if (!active || state != EpisodeState::OPEN) {
        return false;
    }
    if (p_evidence_free) {
        evidence_free_non_contractions += 1;
        return false;
    }
    if (p_shape == TriggerShape::ALL_WITHHELD) {
        withheld_non_contractions += 1;
        return false;
    }
    if (p_shape == TriggerShape::NONE) {
        nc_no_trigger += 1;
    } else {
        nc_mixed_trigger += 1;
    }
    non_contraction_used += 1;
    return true;
}

void Episode::record_escalation(TriggerShape p_live_shape) {
    if (!active || state != EpisodeState::OPEN) {
        return;
    }
    for (int at = int(writes.size()) - 1; at >= 0; --at) {
        EpisodeWrite &write = writes[uint32_t(at)];
        if (write.outcome != OperatorOutcome::PENDING) {
            continue;
        }
        const bool withheld = !write.evidence_free
            && write.trigger_shape == TriggerShape::ALL_WITHHELD;
        write.outcome = withheld ? OperatorOutcome::WITHHELD
                                 : OperatorOutcome::FAILED_TO_CONTRACT;
        write.judged_transition = write.basis;
        write.meter_after = METER_UNMEASURED;
        count_non_contraction(write.trigger_shape, write.evidence_free);
        return;
    }
    count_non_contraction(p_live_shape, false);
}

bool Episode::budget_exhausted() const {
    return active && state == EpisodeState::OPEN
        && (non_contraction_used >= EPISODE_NON_CONTRACTION_BUDGET
            || closure_used >= EPISODE_FULL_CLOSURE_BUDGET);
}

bool Episode::operator_pending(Operator p_operator) const {
    for (int at = int(writes.size()) - 1; at >= 0; --at) {
        if (writes[uint32_t(at)].op != p_operator) {
            continue;
        }
        return writes[uint32_t(at)].outcome == OperatorOutcome::PENDING;
    }
    return false;
}

void Episode::record_breach(int64_t p_transition) {
    if (active) {
        breach_transition = p_transition;
    }
}

void Episode::retire_agreement_run() {
    if (active && state == EpisodeState::OPEN) {
        agreement_run = 0;
        last_comparison_transition = -1;
    }
}

void Episode::repin_generator(
    const Journal &p_journal,
    int64_t p_transition,
    Attribution p_attribution
) {
    if (!active || state != EpisodeState::OPEN) {
        return;
    }
    generator = pin_row(p_journal, p_transition);
    attribution = p_attribution;
}

void Episode::enter_fallback(int64_t p_transition, bool p_demoted) {
    if (!active || state != EpisodeState::OPEN) {
        return;
    }
    state = EpisodeState::FALLBACK;
    fallback_transition = p_transition;
    demoted = p_demoted;
}

void Episode::close_fallback(int64_t p_transition) {
    if (!active || state != EpisodeState::FALLBACK) {
        return;
    }
    closed_transition = p_transition;
    last_closed_id = id;
    last_closed_transition = p_transition;
    last_closure_attribution = attribution;
    last_closure_was_fallback = true;
    active = false;
}

void Episode::suppress_next_flap() {
    flap_suppress_once = true;
}

int Episode::flap_multiplier() const {
    return 1 << std::min(fallback_flap_level, 30);
}

} // namespace netw::predict
