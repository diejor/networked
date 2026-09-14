#include "netw/predict/drive.hpp"

#include <algorithm>

#include "netw/log.hpp"

using namespace godot;

namespace netw::predict {

ConsumeInputPlan plan_consume_input(
    Schedule p_schedule,
    bool p_has_input,
    bool p_has_later_input,
    bool p_has_last_input,
    MissingInput p_missing_policy
) {
    ConsumeInputPlan out;
    if (p_has_input) {
        out.eligible = true;
        out.run = true;
        out.kind = DriveKind::FRESH;
        return out;
    }
    if (!p_has_later_input) {
        return out;
    }
    out.eligible = true;
    out.missing = true;
    out.kind = DriveKind::MISSING;
    out.use_last
        = p_missing_policy == MissingInput::REPEAT_LAST && p_has_last_input;
    out.run = p_schedule == Schedule::FRAME || out.use_last;
    return out;
}

TriggerShape trigger_shape(
    const Wiring &p_wiring,
    const LocalVector<double> &p_field_errors,
    double p_fallback_epsilon
) {
    bool saw_trigger = false;
    bool saw_writable = false;
    for (int at = 0; at < p_wiring.count(); ++at) {
        if (p_wiring.causal[uint32_t(at)] == 0
            || p_wiring.trigger_exclude[uint32_t(at)] != 0
            || at >= int(p_field_errors.size())) {
            continue;
        }
        const double epsilon = p_wiring.epsilon[uint32_t(at)] >= 0.0
            ? p_wiring.epsilon[uint32_t(at)]
            : p_fallback_epsilon;
        if (p_field_errors[uint32_t(at)] <= epsilon) {
            continue;
        }
        saw_trigger = true;
        saw_writable = saw_writable || p_wiring.withheld[uint32_t(at)] == 0;
    }
    if (!saw_trigger) {
        return TriggerShape::NONE;
    }
    return saw_writable ? TriggerShape::MIXED : TriggerShape::ALL_WITHHELD;
}

namespace {

constexpr int64_t QUANTUM_STEPS_MAX = 15;

WritePlan apply_quarantine_plan(
    Slot &r_slot,
    const QuarantinePlan &p_plan,
    Operator p_operator
) {
    WritePlan out;
    if (!p_plan.ready) {
        return out;
    }
    NETW_ASSERT(
        r_slot.episode.active && r_slot.episode.state == EpisodeState::FALLBACK,
        sys::PREDICTION,
        "A quarantine write requires an active fallback episode."
    );
    out.restore = p_plan.payload;
    out.write = p_plan.payload;
    out.basis = p_plan.basis;
    out.op = p_operator;
    out.skip = false;
    r_slot.set_state(p_plan.payload);
    r_slot.episode.record_write(
        out.op,
        out.basis,
        0,
        StringName(),
        0,
        TriggerShape::NONE,
        true
    );
    return out;
}

} // namespace

Tape::Tape() {
    const uint32_t width = uint32_t(TAPE_HISTORY_LIMIT);
    indices.resize(width);
    entry_labels.resize(width);
    fresh.resize(width);
}

void Tape::append(int64_t p_index, int64_t p_label, bool p_fresh) {
    int slot = 0;
    if (count < TAPE_HISTORY_LIMIT) {
        slot = (start + count) % TAPE_HISTORY_LIMIT;
        count += 1;
    } else {
        slot = start;
        start = (start + 1) % TAPE_HISTORY_LIMIT;
    }
    indices[uint32_t(slot)] = p_index;
    entry_labels[uint32_t(slot)] = p_label;
    fresh[uint32_t(slot)] = p_fresh ? 1 : 0;
}

void Tape::clear() {
    start = 0;
    count = 0;
}

int64_t Tape::oldest_index() const {
    if (count == 0) {
        return -1;
    }
    return indices[uint32_t(start)];
}

int64_t Tape::newest_index() const {
    if (count == 0) {
        return -1;
    }
    return indices[uint32_t((start + count - 1) % TAPE_HISTORY_LIMIT)];
}

int64_t Tape::label_of(int64_t p_index) const {
    for (int at = count - 1; at >= 0; --at) {
        const uint32_t slot = uint32_t((start + at) % TAPE_HISTORY_LIMIT);
        if (indices[slot] == p_index) {
            return entry_labels[slot];
        }
    }
    return -1;
}

bool Tape::is_fresh(int64_t p_index) const {
    for (int at = count - 1; at >= 0; --at) {
        const uint32_t slot = uint32_t((start + at) % TAPE_HISTORY_LIMIT);
        if (indices[slot] == p_index) {
            return fresh[slot] != 0;
        }
    }
    return false;
}

void Slot::adopt_timing(const Timing &p_timing) {
    if (p_timing.ticktime > 0.0) {
        tick_delta = p_timing.ticktime;
    }
    frame_index = p_timing.frame;
    if (p_timing.quantum < 1 && !quantum_declaration_warned) {
        quantum_declaration_warned = true;
        NETW_WARN_COND(
            p_timing.quantum < 1,
            sys::PREDICTION,
            "A declared simulation quantum must be a positive integer."
        );
    }
    declared_quantum = p_timing.quantum < 1 ? 1 : p_timing.quantum;
}

void Slot::record_input(int64_t p_tick, int32_t p_c_hash) {
    latest_input_tick = p_tick;
    pending_c_hash = p_c_hash;
}

void Slot::refresh_ack_age() {
    const int64_t age = config.schedule == int(Schedule::FRAME)
        ? next_tape_entry_index - latest_authority_ack - 1
        : last_driven_input_tick - latest_authority_ack;
    stats.ack_age_ticks = age > 0 ? age : 0;
}

bool Slot::horizon_full() {
    refresh_ack_age();
    if (config.role != int(Role::PREDICT)) {
        return false;
    }
    return stats.ack_age_ticks >= ACK_AGE_MAX;
}

void Slot::adopt_timeline(const Ref<NetwTimeline> &p_timeline) {
    timeline = p_timeline;
}

void Slot::reset_entry_history() {
    entry_history = NetwTimeline::create(TAPE_HISTORY_LIMIT);
}

LocalVector<ReplayEntry> Slot::replay_entries(int64_t p_basis) const {
    LocalVector<ReplayEntry> out;
    const bool has_lane = timeline.is_valid();
    if (config.schedule != int(Schedule::FRAME)) {
        for (int64_t tick = p_basis + 1; tick <= latest_input_tick; ++tick) {
            ReplayEntry entry;
            entry.index = tick;
            entry.label = tick;
            entry.input = has_lane ? timeline->input_at(tick) : Dictionary();
            out.push_back(entry);
        }
        return out;
    }
    const int64_t newest = tape.newest_index();
    Dictionary carried
        = has_lane ? timeline->input_at(tape.label_of(p_basis)) : Dictionary();
    for (int64_t index = std::max(tape.oldest_index(), p_basis + 1);
         index <= newest;
         ++index) {
        ReplayEntry entry;
        entry.index = index;
        entry.label = tape.label_of(index);
        if (has_lane && tape.is_fresh(index)) {
            const Dictionary authored = timeline->input_at(entry.label);
            if (!authored.is_empty()) {
                carried = authored;
            }
        }
        entry.input = carried;
        out.push_back(entry);
    }
    return out;
}

Dictionary Slot::state_before(int64_t p_transition) const {
    const Ref<NetwTimeline> &book
        = config.schedule == int(Schedule::FRAME) ? entry_history : timeline;
    return book.is_valid() ? book->state_at(p_transition) : Dictionary();
}

void Slot::mark_carry_dirty(int64_t p_transition) {
    if (carry_rules.is_empty()) {
        return;
    }
    carry_dirty.mark(p_transition);
}

bool Slot::carry_judgeable(int64_t p_transition) const {
    return !carry_dirty.holds(p_transition)
        && !carry_dirty.holds(p_transition + 1);
}

void Slot::trim_history(int64_t p_ack) {
    if (config.schedule == int(Schedule::FRAME)) {
        if (timeline.is_valid()) {
            timeline->trim_before(tape.label_of(p_ack));
        }
        if (entry_history.is_valid()) {
            entry_history->trim_before(p_ack);
        }
        return;
    }
    if (timeline.is_valid()) {
        timeline->trim_before(p_ack);
    }
}

void Slot::reset_tape(int64_t p_epoch) {
    tape_epoch = p_epoch;
    tape.clear();
    // These stores use transition keys, which are unique only within an epoch.
    // Clear them before the new epoch reuses a key.
    journal.clear(p_epoch);
    commands.clear();
    reset_entry_history();
    next_tape_entry_index = 0;
    last_driven_entry_index = -1;
    latest_input_tick = -1;
    last_driven_input_tick = -1;
    last_frame_transition_tick = -1;
    latest_authority_ack = -1;
    pending_c_hash = 0;
    stats.tape_epoch = p_epoch;
    stats.tape_index = -1;
    stats.ack_age_ticks = 0;
}

void RecoveryLedger::resize(int p_count) {
    const uint32_t width = uint32_t(std::max(0, p_count));
    godot::LocalVector<int> kept_triggered(triggered);
    godot::LocalVector<int> kept_repaired(repaired);
    godot::LocalVector<int> kept_contracted(contracted);
    godot::LocalVector<uint8_t> kept_seeded(seeded);
    triggered.resize(width);
    repaired.resize(width);
    contracted.resize(width);
    seeded.resize(width);
    for (uint32_t at = 0; at < width; ++at) {
        const bool carried = at < kept_seeded.size();
        triggered[at] = carried ? kept_triggered[at] : 0;
        repaired[at] = carried ? kept_repaired[at] : 0;
        contracted[at] = carried ? kept_contracted[at] : 0;
        seeded[at] = carried ? kept_seeded[at] : 0;
    }
}

void RecoveryLedger::seed(int p_field) {
    if (p_field >= 0 && uint32_t(p_field) < seeded.size()) {
        seeded[uint32_t(p_field)] = 1;
    }
}

bool RecoveryLedger::has(int p_field) const {
    return p_field >= 0 && uint32_t(p_field) < seeded.size()
        && seeded[uint32_t(p_field)] != 0;
}

void RecoveryLedger::bump_triggered(int p_field) {
    if (p_field >= 0 && uint32_t(p_field) < triggered.size()) {
        triggered[uint32_t(p_field)] += 1;
        seeded[uint32_t(p_field)] = 1;
    }
}

void RecoveryLedger::bump_repaired(int p_field) {
    if (p_field >= 0 && uint32_t(p_field) < repaired.size()) {
        repaired[uint32_t(p_field)] += 1;
        seeded[uint32_t(p_field)] = 1;
    }
}

void RecoveryLedger::bump_contracted(int p_field) {
    if (p_field >= 0 && uint32_t(p_field) < contracted.size()) {
        contracted[uint32_t(p_field)] += 1;
        seeded[uint32_t(p_field)] = 1;
    }
}

int EntityRoster::index_of(const godot::ObjectID &p_id) const {
    for (uint32_t at = 0; at < members.size(); ++at) {
        if (members[at] == p_id) {
            return int(at);
        }
    }
    return -1;
}

bool EntityRoster::add(const godot::ObjectID &p_id) {
    if (index_of(p_id) >= 0) {
        return false;
    }
    members.push_back(p_id);
    return true;
}

bool EntityRoster::erase(const godot::ObjectID &p_id) {
    const int at = index_of(p_id);
    if (at < 0) {
        return false;
    }
    members.remove_at(uint32_t(at));
    return true;
}

void EntityRoster::clear() {
    members.clear();
}

int SubjectBook::index_of(int64_t p_id) const {
    for (uint32_t at = 0; at < ids.size(); ++at) {
        if (ids[at] == p_id) {
            return int(at);
        }
    }
    return -1;
}

bool SubjectBook::note(int64_t p_id, const godot::Callable &p_predictor) {
    const int at = index_of(p_id);
    if (at >= 0) {
        if (predictors[uint32_t(at)] == p_predictor) {
            return false;
        }
        predictors[uint32_t(at)] = p_predictor;
        return true;
    }
    uint32_t slot = 0;
    while (slot < ids.size() && ids[slot] < p_id) {
        slot += 1;
    }
    ids.insert(slot, p_id);
    predictors.insert(slot, p_predictor);
    return true;
}

bool SubjectBook::erase(int64_t p_id) {
    const int at = index_of(p_id);
    if (at < 0) {
        return false;
    }
    ids.remove_at(uint32_t(at));
    predictors.remove_at(uint32_t(at));
    return true;
}

void SubjectBook::clear() {
    ids.clear();
    predictors.clear();
}

godot::Callable SubjectBook::first_valid() const {
    for (uint32_t at = 0; at < predictors.size(); ++at) {
        if (predictors[at].is_valid()) {
            return predictors[at];
        }
    }
    return godot::Callable();
}

void FieldReadings::resize(int p_count) {
    value.resize(uint32_t(std::max(0, p_count)));
    present.resize(uint32_t(std::max(0, p_count)));
    clear();
}

void FieldReadings::clear() {
    for (uint32_t at = 0; at < present.size(); ++at) {
        present[at] = 0;
        value[at] = 0.0;
    }
}

void FieldReadings::note(int p_field, double p_value) {
    if (p_field < 0 || uint32_t(p_field) >= value.size()) {
        return;
    }
    value[uint32_t(p_field)] = p_value;
    present[uint32_t(p_field)] = 1;
}

bool FieldReadings::has(int p_field) const {
    return p_field >= 0 && uint32_t(p_field) < present.size()
        && present[uint32_t(p_field)] != 0;
}

double FieldReadings::at(int p_field, double p_absent) const {
    return has(p_field) ? value[uint32_t(p_field)] : p_absent;
}

bool FieldReadings::empty() const {
    for (uint32_t at = 0; at < present.size(); ++at) {
        if (present[at] != 0) {
            return false;
        }
    }
    return true;
}

void Slot::rewire(const Wiring &p_wiring) {
    wiring = p_wiring;
    report.divergence.resize(p_wiring.count());
    report.tier_error.resize(p_wiring.count());
    recovery_ledger.resize(p_wiring.count());
    state = StateRow();
    state.resize(wiring.count());
    recovery = RecoveryState();
    // A rewire re-keys the transitions the quarantine gathered its evidence
    // over, so a seed retained across one would replay a payload keyed to a
    // numbering that no longer exists. The latch and the proof's target
    // survive, because neither is keyed by transition.
    quarantine.pending_states.clear();
    quarantine.witnesses.clear();
    quarantine.last_basis = -1;
    quarantine.wait_anchor = -1;
    // A rule declared against the replaced field table would be judged under
    // a numbering it never saw.
    carry_rules.clear();
    carry_dirty.clear();
    episode.retire_agreement_run();
    carry.resize(wiring.count());
    last_witness = WitnessSummary();
    last_write_plan = WritePlan();
    last_drive_frame = -1;
    last_quantum = declared_quantum;
}

void Slot::record_idle_drive(int64_t p_label, DriveKind p_kind) {
    stats.drive_seq += 1;
    stats.last_drive_label = p_label;
    stats.last_drive_kind = p_kind;
}

void Slot::record_authoring_clamp() {
    stats.authoring_clamped += 1;
}

void Slot::record_speculation_hold() {
    stats.speculation_held += 1;
}

void Slot::mark_authority_ack(int64_t p_transition) {
    if (p_transition > latest_authority_ack) {
        latest_authority_ack = p_transition;
    }
    refresh_ack_age();
}

void Slot::prepare_tick_tape(int64_t p_tick) {
    if (tape.size() > 0 && tape.newest_index() != p_tick - 1) {
        tape_epoch = (tape_epoch + 1) & 0xFF;
        tape.clear();
        journal.clear(tape_epoch);
    }
    next_tape_entry_index = p_tick;
}

void Slot::author_tape_entry(int64_t p_label, bool p_fresh) {
    tape.append(next_tape_entry_index, p_label, p_fresh);
    last_driven_entry_index = next_tape_entry_index;
    stats.tape_epoch = tape_epoch;
    stats.tape_index = next_tape_entry_index;
    next_tape_entry_index += 1;
}

void Slot::record_drive(
    int64_t p_transition,
    const Fold &p_fold,
    const Dictionary &p_topology,
    const StateStamp &p_pre
) {
    stats.drive_seq += 1;
    stats.last_drive_label = p_fold.label;
    stats.last_drive_kind = p_fold.kind;

    const bool is_new = journal.slot_of(p_transition) < 0;
    const int previous = journal.slot_of(p_transition - 1);
    const uint8_t previous_flags
        = previous < 0 ? uint8_t(0) : journal.flags_of(p_transition - 1);
    const int32_t previous_post
        = previous < 0 ? int32_t(0) : journal.post_fp_of(p_transition - 1);

    JournalOpen row;
    row.label = p_fold.label;
    row.kind = uint8_t(p_fold.kind);
    row.c_hash = pending_c_hash;
    row.pre_fp = p_pre.fp;
    row.pre_families = p_pre.families;
    row.raw_fp = p_pre.raw_fp;
    row.evidence_mask = p_pre.evidence_mask;
    if (is_new) {
        last_quantum = measure_quantum();
        stats.quantum_steps = last_quantum;
        stats.quantum_declared = declared_quantum;
        if (last_quantum != declared_quantum) {
            stats.quantum_faults += 1;
            NETW_DEBUG(
                sys::PREDICTION,
                "quantum mismatch measured=%d declared=%d transition=%d",
                last_quantum,
                declared_quantum,
                p_transition
            );
        }
    }
    row.topo_fp = topology_fingerprint(p_topology, last_quantum);
    journal.open(p_transition, row);

    if (!is_new) {
        return;
    }
    const bool comparable = previous >= 0 && (previous_flags & ROW_CLOSED) != 0
        && (previous_flags & ROW_SUBSTITUTED) == 0;
    if (comparable && previous_post != p_pre.fp) {
        journal.mark_chain_broken(p_transition);
        stats.chain_breaks += 1;
    }
}

int Slot::measure_quantum() {
    const int64_t previous = last_drive_frame;
    last_drive_frame = frame_index;
    if (previous < 0 || frame_index <= 0) {
        return declared_quantum;
    }
    return int(
        std::clamp<int64_t>(frame_index - previous, 0, QUANTUM_STEPS_MAX)
    );
}

DriveRecord Slot::open_drive(
    const Timing &p_timing,
    const Dictionary &p_topology,
    const StateStamp &p_pre
) {
    NETW_ZONE_NC("NetwPredict open drive", colors::PREDICTION);
    adopt_timing(p_timing);
    DriveRecord out;

    const bool framed = config.schedule == int(Schedule::FRAME);
    if (framed) {
        // A frame the clock held bought no simulated time, so it opens no
        // transition. The command lane keeps flowing: holding the world must
        // never hold the player's input.
        if (!p_timing.simulating
            || p_timing.tick <= last_frame_transition_tick) {
            stats.authoring_clamped += 1;
            out.clamped = true;
            return out;
        }
        last_frame_transition_tick = p_timing.tick;
    }

    if (horizon_full()) {
        stats.speculation_held += 1;
        out.held = true;
        return out;
    }

    const Fold decided = prediction_core::fold(
        latest_input_tick,
        last_driven_input_tick,
        p_timing.tick
    );

    // A TICK drive is its own transition, so the tape entry is degenerate:
    // index, label and tick are the same number and every entry is fresh. The
    // lane codec implies contiguous indices, so a clock re-anchor that gaps the
    // tick sequence starts a new epoch instead of straddling it.
    if (!framed) {
        prepare_tick_tape(p_timing.tick);
    }
    const int64_t transition = next_tape_entry_index;
    author_tape_entry(decided.label, framed ? decided.fresh : true);
    if (!framed || decided.fresh) {
        last_driven_input_tick = framed ? decided.label : p_timing.tick;
    }
    record_drive(transition, decided, p_topology, p_pre);

    out.transition = transition;
    out.label = decided.label;
    out.kind = framed ? decided.kind : DriveKind::FRESH;
    out.fresh = framed ? decided.fresh : true;
    out.ran = true;
    return out;
}

DriveRecord Slot::replay_drive(
    const Timing &p_timing,
    const Dictionary &p_topology,
    int64_t p_transition,
    int64_t p_label,
    DriveKind p_kind,
    const StateStamp &p_pre,
    bool p_authoring
) {
    NETW_ZONE_NC("NetwPredict replay drive", colors::PREDICTION);
    adopt_timing(p_timing);

    Fold replayed;
    replayed.label = p_label;
    replayed.kind = p_kind;
    replayed.fresh = p_kind == DriveKind::FRESH;

    int64_t transition = p_transition;
    if (p_authoring && config.schedule != int(Schedule::FRAME)) {
        prepare_tick_tape(p_timing.tick);
        transition = next_tape_entry_index;
        author_tape_entry(p_label, true);
        last_driven_input_tick = p_timing.tick;
    }
    record_drive(transition, replayed, p_topology, p_pre);

    DriveRecord out;
    out.transition = transition;
    out.label = p_label;
    out.kind = p_kind;
    out.fresh = replayed.fresh;
    out.ran = true;
    return out;
}

bool Slot::record_evidence(
    int64_t p_transition,
    int64_t p_environment_epoch,
    const Dictionary &p_environment,
    const Dictionary &p_topology,
    const LocalVector<WitnessContact> &p_contacts,
    bool p_sleeping
) {
    NETW_ZONE_NC("NetwPredict record evidence", colors::PREDICTION);
    NETW_ERR_COND_V(
        journal.slot_of(p_transition) < 0
            || (journal.flags_of(p_transition) & ROW_CLOSED) != 0,
        false,
        sys::PREDICTION,
        "Solve evidence needs an open journal row at transition %d.",
        p_transition
    );
    NETW_ERR_COND_V(
        !config.witness && !p_contacts.is_empty(),
        false,
        sys::PREDICTION,
        "Solve evidence carries contacts without a witness declaration."
    );
    WitnessSummary witness;
    if (config.witness) {
        witness = summarize_witness(p_contacts, p_sleeping);
        NETW_ERR_COND_V(
            !witness.valid,
            false,
            sys::PREDICTION,
            "Solve evidence contains an invalid witness."
        );
        last_witness = witness;
    }
    journal.mark_e_digest(
        p_transition,
        environment_digest(p_environment_epoch, p_environment)
    );
    const uint8_t evidence_mask = uint8_t(
        journal.evidence_of(p_transition).evidence_mask
        | (config.witness ? EVIDENCE_WITNESS : 0)
    );
    uint8_t witness_state = 0;
    if (config.witness) {
        witness_state = uint8_t(
            (p_sleeping ? WITNESS_SLEEPING : 0)
            | (witnessed_sleeping && !p_sleeping ? WITNESS_WOKE : 0)
        );
        witnessed_sleeping = p_sleeping;
        witnessed_before = true;
    }
    journal.mark_solve(
        p_transition,
        topology_fingerprint(p_topology, last_quantum),
        witness.fingerprint,
        evidence_mask,
        witness.class_bits,
        witness.realization_bits,
        witness_state
    );
    NETW_TRACE(
        sys::PREDICTION,
        "evidence transition=%d quantum=%d witness=%d",
        p_transition,
        last_quantum,
        witness.contact_count
    );
    return true;
}

void Slot::close_drive(int64_t p_transition, const StateStamp &p_post) {
    journal.close(p_transition, p_post.fp, p_post.families);
}

void Slot::declare_skipped(int64_t p_transition, int64_t p_label) {
    if (journal.slot_of(p_transition) >= 0) {
        journal.mark_superseded(p_transition);
        return;
    }
    JournalOpen row;
    row.label = p_label;
    row.kind = uint8_t(DriveKind::MISSING);
    journal.open(p_transition, row);
    journal.mark_substituted(p_transition);
}

void Slot::acknowledge(int64_t p_transition, bool p_matched) {
    if (p_transition > latest_authority_ack) {
        latest_authority_ack = p_transition;
    }
    journal.mark_ack(p_transition, p_matched);
    refresh_ack_age();
}

AckVerdict Slot::admit_ack(
    int64_t p_transition,
    const EvidenceRow &p_peer,
    bool p_substituted
) {
    const AckVerdict out
        = predict::admit_ack(journal, p_transition, p_peer, p_substituted);
    if (!out.compared) {
        return out;
    }
    compare_stats.fp_verified += 1;
    if (out.exact == ExactVerdict::UNEQUAL) {
        compare_stats.fp_mismatches += 1;
        if (compare_stats.first_divergent_transition < 0) {
            compare_stats.first_divergent_transition = p_transition;
        }
    }
    if (p_transition > latest_authority_ack) {
        latest_authority_ack = p_transition;
    }
    refresh_ack_age();
    return out;
}

StateVerdict Slot::compare(
    int64_t p_recv_tick,
    int64_t p_transition,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const LocalVector<double> &p_correction_tolerances,
    const LocalVector<double> &p_meter_tolerances,
    double p_fallback_epsilon,
    bool p_stream_reconstructed,
    bool p_ack_domain_confirmed
) {
    last_state_verdict = compare_state(
        wiring,
        journal,
        p_recv_tick,
        p_transition,
        p_predicted,
        p_authority,
        p_correction_tolerances,
        p_meter_tolerances,
        p_fallback_epsilon,
        p_stream_reconstructed
    );
    if (p_ack_domain_confirmed && p_transition > latest_authority_ack) {
        latest_authority_ack = p_transition;
        refresh_ack_age();
    }
    if (last_state_verdict.settled) {
        if (quarantine.probation_pending && p_stream_reconstructed) {
            const bool corrected = last_state_verdict.corrected;
            quarantine.finish_probation(corrected);
            if (corrected) {
                journal.mark_divergent(p_transition);
                episode.suppress_next_flap();
                episode.open(
                    journal,
                    p_transition,
                    last_state_verdict.attribution
                );
                enter_quarantine(p_transition, p_stream_reconstructed);
            }
            return last_state_verdict;
        }
        if (last_state_verdict.corrected) {
            journal.mark_divergent(p_transition);
            if (!episode.active || episode.state == EpisodeState::CLOSED) {
                episode.open(
                    journal,
                    p_transition,
                    last_state_verdict.attribution
                );
            } else {
                episode.record_divergence(journal, p_transition);
            }
        }
        if (episode.active && episode.state == EpisodeState::OPEN) {
            episode.record_comparison(
                p_transition,
                last_state_verdict.meter,
                !last_state_verdict.corrected,
                int(stats.ack_age_ticks)
            );
            if (last_state_verdict.corrected && episode.budget_exhausted()) {
                enter_quarantine(p_transition, p_stream_reconstructed);
            }
        }
    }
    return last_state_verdict;
}

void Slot::set_state(const StateRow &p_state) {
    state = StateRow();
    state.resize(wiring.count());
    apply_restore(state, p_state);
}

WritePlan Slot::recover(const RecoveryRequest &p_request) {
    RecoveryRequest effective = p_request;
    effective.contact_window
        = effective.contact_window || recovery.window_contains(effective.basis);
    effective.suppressed = effective.suppressed
        || recovery.suppressed_at(effective.current_label);
    last_write_plan = stage_recovery(wiring, config, effective, recovery);
    const TriggerShape shape = trigger_shape(
        wiring,
        effective.field_errors,
        effective.fallback_epsilon
    );
    if (last_write_plan.escalated) {
        episode.record_escalation(shape);
    }
    if (last_write_plan.skip) {
        recovery_stats.recoveries_skipped += 1;
        return last_write_plan;
    }
    set_state(p_request.current);
    apply_restore(state, last_write_plan.restore);
    recovery_stats.corrections += 1;
    recovery_stats.teleports += last_write_plan.teleport ? 1 : 0;
    recovery_stats.escalations += last_write_plan.escalated ? 1 : 0;
    episode.record_write(
        last_write_plan.op,
        last_write_plan.basis,
        0,
        StringName(),
        effective.ack_age_ticks,
        shape
    );
    return last_write_plan;
}

WritePlan Slot::quarantine_state(
    int64_t p_tick,
    int64_t p_basis,
    const StateRow &p_payload,
    bool p_whole
) {
    QuarantinePlan selected
        = quarantine.admit_state(p_tick, p_basis, p_payload, p_whole);
    episode.quarantine_clean_run = quarantine.clean_run;
    if (selected.ready) {
        selected.payload = advance_seed(
            wiring,
            config,
            selected.payload,
            transition_span(selected.basis),
            tick_delta
        );
    }
    last_write_plan = apply_quarantine_plan(*this, selected, Operator::RESEED);
    if (!last_write_plan.skip) {
        episode.reseed_transition = selected.basis;
    }
    return last_write_plan;
}

WritePlan Slot::quarantine_witness(int64_t p_basis, int p_bits) {
    QuarantinePlan selected = quarantine.admit_witness(p_basis, p_bits);
    episode.quarantine_clean_run = quarantine.clean_run;
    if (selected.ready) {
        selected.payload = advance_seed(
            wiring,
            config,
            selected.payload,
            transition_span(selected.basis),
            tick_delta
        );
    }
    last_write_plan = apply_quarantine_plan(*this, selected, Operator::RESEED);
    if (!last_write_plan.skip) {
        episode.reseed_transition = selected.basis;
    }
    return last_write_plan;
}

void Slot::confirm_reseed_epoch() {
    quarantine.confirm_epoch();
}

WritePlan Slot::align_reseed(
    int64_t p_transition,
    const StateRow &p_payload,
    int64_t p_ignore_through
) {
    QuarantinePlan aligned
        = quarantine.align(p_transition, p_payload, p_ignore_through);
    if (aligned.ready) {
        aligned.payload = advance_seed(
            wiring,
            config,
            aligned.payload,
            transition_span(aligned.basis),
            tick_delta
        );
    }
    last_write_plan
        = apply_quarantine_plan(*this, aligned, Operator::RESEED_ALIGN);
    if (!last_write_plan.skip) {
        if (config.correction == int(CorrectionMode::REPLAY)
            && p_ignore_through > aligned.basis) {
            last_write_plan.replay_from = aligned.basis + 1;
            last_write_plan.replay_through = p_ignore_through;
        }
        episode.aligned_transition = aligned.basis;
        episode.close_fallback(aligned.basis);
    }
    return last_write_plan;
}

bool Slot::admit_post_reseed(int64_t p_basis) {
    return quarantine.admit_post_reseed(p_basis);
}

void Slot::adopt_alignment(int64_t p_transition, int64_t p_ignore_through) {
    quarantine.adopt_alignment(p_transition, p_ignore_through);
    episode.aligned_transition = p_transition;
    episode.close_fallback(p_transition);
}

bool Slot::finish_probation(bool p_corrected) {
    return quarantine.finish_probation(p_corrected);
}

int Slot::transition_span(int64_t p_basis) const {
    if (p_basis < 0) {
        return 0;
    }
    if (config.schedule == int(Schedule::FRAME)) {
        return int(std::max<int64_t>(0, next_tape_entry_index - p_basis - 1));
    }
    return int(std::max<int64_t>(0, last_driven_input_tick - p_basis));
}

void Slot::latch_quarantine(
    int64_t p_transition,
    bool p_stream_reconstructed,
    Attribution p_attribution,
    bool p_demoted
) {
    // Re-entering a slot already latched in fallback would reprice the
    // clean-run target against a flap the slot did not have.
    if (quarantine.latched && episode.active
        && episode.state == EpisodeState::FALLBACK) {
        return;
    }
    // Fallback is a state of an open episode, so a caller quarantining for a
    // reason no comparison judged supplies the episode the quarantine is a
    // state of. Latching without one produces a proof whose reseed has no
    // episode to write into.
    if (!episode.active || episode.state != EpisodeState::OPEN) {
        episode.open(journal, p_transition, p_attribution);
    }
    enter_quarantine(p_transition, p_stream_reconstructed, p_demoted);
}

void Slot::enter_quarantine(
    int64_t p_transition,
    bool p_stream_reconstructed,
    bool p_demoted
) {
    episode.enter_fallback(p_transition, p_demoted);
    quarantine.enter(
        int(stats.ack_age_ticks),
        episode.flap_multiplier(),
        p_stream_reconstructed
    );
    episode.resume_ack_age = int(stats.ack_age_ticks);
    episode.quarantine_target = quarantine.target;
}

void Slot::open_recovery_window(int64_t p_label, int p_cooldown) {
    recovery.open_window(p_label, p_cooldown);
}

void Slot::suppress_recovery_until(int64_t p_label, int p_cooldown) {
    recovery.suppress_until(p_label, p_cooldown);
}

} // namespace netw::predict
