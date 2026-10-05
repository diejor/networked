#include "netw/predict/drive.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

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
            "Prediction was given %d physics steps per tick, which is not "
            "positive, so it uses 1.",
            p_timing.quantum
        );
    }
    declared_quantum = p_timing.quantum < 1 ? 1 : p_timing.quantum;
}

void Slot::record_input(int64_t p_tick, int32_t p_c_hash) {
    recorded_input_tick = p_tick;
    pending_c_hash = p_c_hash;
}

void Slot::refresh_ack_age() {
    const int64_t age = config.schedule == int(Schedule::FRAME)
        ? next_tape_entry_index - latest_authority_ack - 1
        : drive_input_tick - std::max(latest_authority_ack, ack_origin);
    stats.ack_age_ticks = age > 0 ? age : 0;
}

bool Slot::horizon_full() {
    refresh_ack_age();
    if (config.role != int(Role::PREDICT)) {
        return false;
    }
    return stats.ack_age_ticks >= ACK_AGE_MAX;
}

void Slot::open_lane() {
    inputs.drop_origin(uint8_t(CellProvenance::AUTHORED), true);
    input_floor = -1;
    lane_open = true;
}

void Slot::close_lane() {
    inputs.drop_origin(uint8_t(CellProvenance::AUTHORED), true);
    input_floor = -1;
    lane_open = false;
}

int32_t Slot::topology_fp(const Dictionary &p_topology) {
    if (topology_fp_quantum != last_quantum
        || topology_fp_facts != p_topology) {
        topology_fp_facts = p_topology.duplicate();
        topology_fp_quantum = last_quantum;
        topology_fp_value = topology_fingerprint(p_topology, last_quantum);
    }
    return topology_fp_value;
}

bool Slot::record_authored_row(
    int64_t p_key,
    int64_t p_label,
    bool p_fresh,
    const StateRow &p_input
) {
    return inputs.record_row(
        input_codec,
        p_key,
        p_label,
        p_fresh,
        p_input,
        uint8_t(CellProvenance::AUTHORED)
    );
}

void Slot::unacknowledged_row(int64_t p_key, StateRow &r_input) const {
    if (!lane_open || inputs.origin_of(p_key) != int(CellProvenance::AUTHORED)
        || !inputs.read_row(input_codec, p_key, r_input)) {
        r_input.resize(input_codec.count());
    }
}

void Slot::authored_row(int64_t p_key, StateRow &r_input) const {
    if (p_key < input_floor) {
        r_input.resize(input_codec.count());
        return;
    }
    unacknowledged_row(p_key, r_input);
}

bool Slot::holds_authored(int64_t p_key) const {
    return lane_open && inputs.origin_of(p_key) == int(CellProvenance::AUTHORED)
        && inputs.carries_fields(p_key);
}

bool Slot::has_authored(int64_t p_key) const {
    return p_key >= input_floor && holds_authored(p_key);
}

int64_t Slot::newest_authored() const {
    const int64_t newest = inputs.newest_key();
    for (int64_t key = newest;
         key >= 0 && key > newest - int64_t(inputs.capacity());
         --key) {
        if (inputs.origin_of(key) == int(CellProvenance::AUTHORED)) {
            return key;
        }
    }
    return -1;
}

void Slot::authored_ticks(
    int64_t p_from,
    int64_t p_to,
    LocalVector<int64_t> &r_ticks
) const {
    NETW_ZONE_LEAF(
        "Predict input window",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    r_ticks.clear();
    if (!lane_open) {
        return;
    }
    for (int64_t tick = std::max(p_from, input_floor); tick <= p_to; ++tick) {
        if (inputs.origin_of(tick) == int(CellProvenance::AUTHORED)) {
            r_ticks.push_back(tick);
        }
    }
}

bool Slot::admit_command(
    int64_t p_key,
    int64_t p_label,
    bool p_fresh,
    const StateRow &p_command
) {
    return inputs.origin_of(p_key) < int(CellProvenance::AUTHORED)
        && inputs.record_row(
            input_codec,
            p_key,
            p_label,
            p_fresh,
            p_command,
            uint8_t(CellProvenance::AUTHORED)
        );
}

bool Slot::record_joint_command(
    int64_t p_key,
    const StateRow &p_command,
    CellProvenance p_provenance
) {
    if (inputs.origin_of(p_key) >= int(p_provenance)) {
        return false;
    }
    return inputs.record_row(
        input_codec,
        p_key,
        p_key,
        true,
        p_command,
        uint8_t(p_provenance)
    );
}

int Slot::command_depth_from(int64_t p_cursor) const {
    if (p_cursor < 0) {
        return 0;
    }
    int depth = 0;
    while (holds_command(p_cursor + depth)) {
        depth += 1;
    }
    return depth;
}

bool Slot::holds_command(int64_t p_key) const {
    return inputs.origin_of(p_key) == int(CellProvenance::AUTHORED);
}

PackedInt64Array Slot::command_keys() const {
    PackedInt64Array out;
    const int64_t newest = inputs.newest_key();
    if (newest < 0) {
        return out;
    }
    for (int64_t key = std::max(int64_t(0), inputs.oldest_key()); key <= newest;
         ++key) {
        if (holds_command(key)) {
            out.push_back(key);
        }
    }
    return out;
}

LocalVector<ReplayEntry> Slot::replay_entries(int64_t p_basis) const {
    LocalVector<ReplayEntry> out;
    if (config.schedule != int(Schedule::FRAME)) {
        for (int64_t tick = p_basis + 1; tick <= recorded_input_tick; ++tick) {
            ReplayEntry entry;
            entry.index = tick;
            entry.label = tick;
            entry.input_key = tick;
            out.push_back(entry);
        }
        return out;
    }
    const int64_t newest = tape.newest_index();
    int64_t carried = p_basis;
    for (int64_t index = std::max(tape.oldest_index(), p_basis + 1);
         index <= newest;
         ++index) {
        ReplayEntry entry;
        entry.index = index;
        entry.label = tape.label_of(index);
        if (tape.is_fresh(index) && has_authored(index)) {
            carried = index;
        }
        entry.input_key = carried;
        out.push_back(entry);
    }
    return out;
}

int64_t Slot::label_for(int64_t p_key) const {
    if (config.schedule != int(Schedule::FRAME)) {
        return p_key;
    }
    const int64_t label = tape.label_of(p_key - 1);
    return label < 0 ? -1 : label + 1;
}

bool Slot::record_state_row(int64_t p_key, const StateRow &p_state) {
    const bool framed = config.schedule == int(Schedule::FRAME);
    return book.record_row(
        wiring.codec,
        p_key,
        label_for(p_key),
        !framed || tape.is_fresh(p_key - 1),
        p_state
    );
}

bool Slot::joint_state(int64_t p_transition, StateRow &r_state) const {
    return p_transition > joint.state_floor
        && book.read_row(wiring.codec, p_transition + 1, r_state)
        && r_state.any();
}

int64_t Slot::joint_newest_state(int64_t p_transition) const {
    const int64_t key = book.newest_at_or_before(p_transition + 1);
    return key - 1 > joint.state_floor ? key - 1 : -1;
}

int64_t Slot::joint_oldest_state() const {
    const int64_t key = book.oldest_key();
    if (key < 0) {
        return -1;
    }
    return std::max(key - 1, joint.state_floor + 1);
}

void Slot::clear_joint_cells() {
    inputs.drop_origin(uint8_t(CellProvenance::AUTHORED), false);
    inputs.forget_relays();
    joint.clear_cells(book.newest_key() - 1);
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
    book.trim_before(p_ack);
    if (lane_open) {
        input_floor = std::max(input_floor, p_ack);
    }
}

void Slot::reset_tape(int64_t p_epoch) {
    tape_epoch = p_epoch;
    tape.clear();
    journal.clear(p_epoch);
    if (config.schedule == int(Schedule::FRAME)) {
        inputs.clear();
        input_floor = -1;
        book.clear();
        joint.state_floor = -1;
    }
    next_tape_entry_index = 0;
    last_driven_entry_index = -1;
    recorded_input_tick = -1;
    drive_input_tick = -1;
    drive_frame_tick = -1;
    latest_authority_ack = -1;
    ack_origin = -1;
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
    book.reset(wiring.codec);
    joint.state_floor = -1;
    report.divergence.resize(p_wiring.count());
    report.tier_error.resize(p_wiring.count());
    recovery_ledger.resize(p_wiring.count());
    state = StateRow();
    state.resize(wiring.count());
    recovery = RecoveryState();
    quarantine.pending_states.clear();
    quarantine.witnesses.clear();
    quarantine.last_basis = -1;
    quarantine.wait_anchor = -1;
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
                "A tick ran %d physics steps where %d were expected (tick %d).",
                last_quantum,
                declared_quantum,
                p_transition
            );
        }
    }
    row.topo_fp = topology_fp(p_topology);
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
    NETW_ZONE_LEAF(
        "Predict open drive",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    adopt_timing(p_timing);
    DriveRecord out;

    const bool framed = config.schedule == int(Schedule::FRAME);
    if (framed) {
        if (!p_timing.simulating || p_timing.tick <= drive_frame_tick) {
            stats.authoring_clamped += 1;
            out.clamped = true;
            return out;
        }
        drive_frame_tick = p_timing.tick;
    }

    if (horizon_full()) {
        stats.speculation_held += 1;
        out.held = true;
        return out;
    }

    const Fold decided = prediction_core::fold(
        recorded_input_tick,
        drive_input_tick,
        p_timing.tick
    );

    if (!framed) {
        prepare_tick_tape(p_timing.tick);
    }
    const int64_t transition = next_tape_entry_index;
    author_tape_entry(decided.label, framed ? decided.fresh : true);
    if (!framed || decided.fresh) {
        drive_input_tick = framed ? decided.label : p_timing.tick;
    }
    if (!framed && ack_origin < 0) {
        ack_origin = p_timing.tick - 1;
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
    NETW_ZONE_LEAF(
        "Predict replay drive",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
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
        drive_input_tick = p_timing.tick;
        if (ack_origin < 0) {
            ack_origin = p_timing.tick - 1;
        }
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
    NETW_ZONE_LEAF(
        "Predict record evidence",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    NETW_ERR_COND_V(
        journal.slot_of(p_transition) < 0
            || (journal.flags_of(p_transition) & ROW_CLOSED) != 0,
        false,
        sys::PREDICTION,
        "Prediction got physics results for tick %d, which it has no open "
        "record of.",
        p_transition
    );
    NETW_ERR_COND_V(
        !config.witness && !p_contacts.is_empty(),
        false,
        sys::PREDICTION,
        "Prediction got contacts for an entity with no "
        "NetwPrediction.witness_contacts, so it ignored them."
    );
    WitnessSummary witness;
    if (config.witness) {
        witness = summarize_witness(p_contacts, p_sleeping);
        if (!witness.valid) {
            return false;
        }
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
        topology_fp(p_topology),
        witness.fingerprint,
        evidence_mask,
        witness.class_bits,
        witness.realization_bits,
        witness_state
    );
    NETW_TRACE(
        sys::PREDICTION,
        "Recorded tick %d after %d physics steps with %d contacts.",
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

const StateVerdict &Slot::compare(
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
    compare_state(
        wiring,
        journal,
        p_recv_tick,
        p_transition,
        p_predicted,
        p_authority,
        p_correction_tolerances,
        p_meter_tolerances,
        p_fallback_epsilon,
        p_stream_reconstructed,
        last_state_verdict
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
    state.resize(wiring.count());
    for (uint32_t at = 0; at < state.values.size(); ++at) {
        state.values[at] = Variant();
    }
    apply_restore(state, p_state);
}

const WritePlan &Slot::recover(const RecoveryRequest &p_request) {
    RecoveryRequest &effective = recovering.effective;
    effective = p_request;
    effective.contact_window
        = effective.contact_window || recovery.window_contains(effective.basis);
    effective.suppressed = effective.suppressed
        || recovery.suppressed_at(effective.current_label);
    stage_recovery_into(wiring, config, effective, recovery, last_write_plan);
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
    NETW_TALLY(profile::names::PREDICT_CORRECTIONS, 1);
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
    return int(std::max<int64_t>(0, drive_input_tick - p_basis));
}

void Slot::latch_quarantine(
    int64_t p_transition,
    bool p_stream_reconstructed,
    Attribution p_attribution,
    bool p_demoted
) {
    if (quarantine.latched && episode.active
        && episode.state == EpisodeState::FALLBACK) {
        return;
    }
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
