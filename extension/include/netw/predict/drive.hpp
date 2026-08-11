#pragma once

/* One predicting entity's drive path: the tape it authors, the cursors that
 * order it, and the journal row each drive opens and closes.
 *
 * The engine owns the bookkeeping and never the body. A caller supplies the
 * command it authored and the state the drive produced as fingerprints, and
 * gets back the one drive the pass applied. That split is what lets the same
 * path run under a shell, under a law with no shell, and under a rig with no
 * tree, because none of them differ in anything the engine decides.
 *
 * [codeblock]
 * slot.record_input(tick, c_hash);
 * const DriveRecord drive = slot.open_drive(timing, pre);
 * if (drive.ran) {
 *     // the caller advances the body here
 *     slot.close_drive(drive.transition, post);
 * }
 * [/codeblock]
 */

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "netw/predict/carry.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/episode.hpp"
#include "netw/predict/joint.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/quarantine.hpp"
#include "netw/predict/recovery.hpp"
#include "netw/predict/sensors.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/prediction_core.hpp"

namespace netw {

using namespace godot;

namespace predict {

constexpr int TAPE_HISTORY_LIMIT = 256;

// The owner may speculate through at most one quarter of the history ring,
// about one second at the standard rate, leaving the rest for acknowledgement
// and recovery evidence.
constexpr int ACK_AGE_MAX = 64;

/* The contiguous entries a FRAME drive authors, one per pass.
 *
 * Three columns rather than a row per entry, because the lane codec reads one
 * column at a time and an entry is never handled alone.
 */
class Tape {
    LocalVector<int64_t> indices;
    LocalVector<int64_t> entry_labels;
    LocalVector<uint8_t> fresh;
    int start = 0;
    int count = 0;

public:
    Tape();

    void append(int64_t p_index, int64_t p_label, bool p_fresh);
    void clear();

    int size() const {
        return count;
    }

    int64_t newest_index() const;

    // -1 when the entry has fallen out of the ring, which is the same absence
    // a caller reading past the horizon already handles.
    int64_t label_of(int64_t p_index) const;
    bool is_fresh(int64_t p_index) const;
};

// What a pass carries in from the clock, taken as an argument so a core can be
// driven at a tick nothing scheduled.
struct Timing {
    int64_t tick = 0;
    int64_t frame = 0;
    double delta = 0.0;
    double ticktime = 0.0;
    int quantum = 1;
    bool simulating = true;
};

/* The state the caller fingerprinted, whole-state and by family.
 *
 * The engine never reads a property, so a state reaches it already reduced to
 * what a comparison is entitled to judge.
 */
struct StateStamp {
    int32_t fp = 0;
    FamilyFingerprints families;
    int32_t raw_fp = 0;
    uint8_t evidence_mask = 0;
};

/* What one pass did, which is the whole of what a caller has to act on.
 *
 * `ran` false with `held` or `clamped` set is a refusal the pass earned rather
 * than an error: the horizon was full, or the schedule declined the frame.
 */
struct DriveRecord {
    int64_t transition = -1;
    int64_t label = -1;
    DriveKind kind = DriveKind::NONE;
    bool ran = false;
    bool fresh = false;
    bool held = false;
    bool clamped = false;
};

struct ConsumeInputPlan {
    bool eligible = false;
    bool missing = false;
    bool run = false;
    bool use_last = false;
    DriveKind kind = DriveKind::NONE;
};

ConsumeInputPlan plan_consume_input(
    Schedule p_schedule,
    bool p_has_input,
    bool p_has_later_input,
    bool p_has_last_input,
    MissingInput p_missing_policy
);

// The counters a shell publishes and a plot reads, kept as the decision
// already computed them so an instrument never recomputes a drive.
struct DriveStats {
    int64_t drive_seq = 0;
    int64_t last_drive_label = -1;
    DriveKind last_drive_kind = DriveKind::NONE;
    int64_t tape_epoch = 0;
    int64_t tape_index = -1;
    int64_t ack_age_ticks = 0;
    int authoring_clamped = 0;
    int speculation_held = 0;
    int chain_breaks = 0;
    int quantum_steps = 1;
    int quantum_declared = 1;
    int quantum_faults = 0;
};

/* One engine, as a row of the pool.
 *
 * The invariant the whole type exists for: a transition is injective within a
 * tape epoch. A clock that jumps past the contiguous lane opens a new epoch and
 * clears the journal with it, because a journal outliving its epoch would hold
 * two rows claiming one transition.
 */
struct Slot {
    Config config;
    Wiring wiring;
    FieldCodec input_codec;
    // The object the plane reads input onto and captures state from. An
    // unbound port is a slot that does no property I/O at all, which is the
    // ordinary shape for a test that drives state rows directly.
    ObjectPort port;
    /* Where this slot falls in the pass, which is a determinism contract.
     *
     * The key is the entity's ROUTE, the session-global integer identity that
     * replicates to every peer, so two peers step the same roster in the same
     * order without exchanging anything about it. An unkeyed slot sorts after
     * every keyed one and ties break on the slot, so the order is total
     * whatever the shell has declared so far.
     */
    int64_t order_key = -1;
    // Which declared fields the bound owner actually answers for, resolved at
    // the bind rather than per pass: the declaration is fixed and so is the
    // owner, so the answer cannot change without one of them changing.
    LocalVector<uint8_t> owner_has_state;
    LocalVector<uint8_t> owner_has_input;
    // Whether the bound owner is a solver body, which is the one thing about
    // an owner the pool answers that is not a property. A body's transition is
    // a physics step, so the correction it can honour is not the one a
    // kinematic body can.
    bool owner_solves = false;
    /* The behaviour a game injects, stored per slot rather than per pool.
     *
     * `simulate` is the step itself, adopted from the owner's
     * `_network_tick` unless a caller set one explicitly, and an explicit one
     * always wins. The rest are evidence the pool cannot sample for itself.
     */
    Callable simulate;
    Callable witness;
    Callable corridor;
    HashMap<StringName, Callable> sensors;
    HashMap<StringName, Callable> carry_rules;
    Journal journal;
    Tape tape;
    DriveStats stats;
    CompareStats compare_stats;
    StateVerdict last_state_verdict;
    StateRow state;
    Episode episode;
    Island island;
    JointTrack joint;
    JointStats joint_stats;
    Quarantine quarantine;
    CarryTrack carry;
    WitnessSummary last_witness;
    RecoveryState recovery;
    RecoveryStats recovery_stats;
    WritePlan last_write_plan;

    int64_t tape_epoch = 0;
    int64_t next_tape_entry_index = 0;
    int64_t last_driven_entry_index = -1;

    int64_t latest_input_tick = -1;
    int64_t last_driven_input_tick = -1;
    int64_t last_frame_transition_tick = -1;
    int64_t latest_authority_ack = -1;

    int64_t frame_index = 0;
    int64_t last_drive_frame = -1;
    int last_quantum = 1;
    int declared_quantum = 1;
    double tick_delta = 1.0 / 60.0;

    int32_t pending_c_hash = 0;
    // A solve that woke into its transition is one the solver started rather
    // than continued, so the previous solve's rest state has to survive it.
    bool witnessed_sleeping = false;
    bool witnessed_before = false;
    bool open = false;
    bool quantum_declaration_warned = false;

    void adopt_timing(const Timing &p_timing);
    void reset_tape(int64_t p_epoch);
    void record_input(int64_t p_tick, int32_t p_c_hash);

    DriveRecord open_drive(
        const Timing &p_timing,
        const Dictionary &p_topology,
        const StateStamp &p_pre
    );

    /* Journals a transition the owner already authored and this slot is
     * replaying, which is what a server consuming a command lane does.
     *
     * A replay neither folds nor clamps nor authors a tape entry, because all
     * three are the owner's answers and they arrived with the entry. The
     * caller therefore names the transition, the label and the kind, and the
     * clamp `open_drive` applies under FRAME is exactly what must not run
     * here: a consume pass replays every queued entry at one timing, where an
     * authoring pass opens at most one transition per tick.
     */
    DriveRecord replay_drive(
        const Timing &p_timing,
        const Dictionary &p_topology,
        int64_t p_transition,
        int64_t p_label,
        DriveKind p_kind,
        const StateStamp &p_pre
    );

    bool record_evidence(
        int64_t p_transition,
        int64_t p_environment_epoch,
        const Dictionary &p_environment,
        const Dictionary &p_topology,
        const LocalVector<WitnessContact> &p_contacts,
        bool p_sleeping
    );
    void close_drive(int64_t p_transition, const StateStamp &p_post);

    /* Journals a transition authority declared it never ran.
     *
     * A transition the slot never opened is sealed as a substitution, because
     * a declared skip names exactly which command did not run where a silent
     * gap is indistinguishable from a lost frame. One the slot did open is
     * overlaid instead, so the evidence it already recorded stays readable
     * behind the verdict that retired it.
     */
    void declare_skipped(int64_t p_transition, int64_t p_label);

    void acknowledge(int64_t p_transition, bool p_matched);
    AckVerdict admit_ack(
        int64_t p_transition,
        const EvidenceRow &p_peer,
        bool p_substituted
    );
    StateVerdict compare(
        int64_t p_recv_tick,
        int64_t p_transition,
        const StateRow &p_predicted,
        const StateRow &p_authority,
        const LocalVector<double> &p_correction_tolerances,
        const LocalVector<double> &p_meter_tolerances,
        double p_fallback_epsilon,
        bool p_stream_reconstructed,
        bool p_ack_domain_confirmed
    );
    void set_state(const StateRow &p_state);
    WritePlan recover(const RecoveryRequest &p_request);
    WritePlan quarantine_state(
        int64_t p_tick,
        int64_t p_basis,
        const StateRow &p_payload,
        bool p_whole
    );
    WritePlan quarantine_witness(int64_t p_basis, int p_bits);
    void confirm_reseed_epoch();
    WritePlan align_reseed(
        int64_t p_transition,
        const StateRow &p_payload,
        int64_t p_ignore_through
    );
    bool admit_post_reseed(int64_t p_basis);
    // The alignment a SHELL staged: the pool records where it landed and
    // retires the fallback, without planning the payload. `align_reseed` is
    // the same bookkeeping for an alignment the pool planned itself.
    void adopt_alignment(int64_t p_transition, int64_t p_ignore_through);
    bool finish_probation(bool p_corrected);
    void open_recovery_window(int64_t p_label, int p_cooldown);
    void suppress_recovery_until(int64_t p_label, int p_cooldown);

    // The horizon bounds how far a slot may run ahead of what authority has
    // confirmed, so it applies only where there is a distance to bound. A slot
    // that IS the authority for its own commands confirms nothing back to
    // itself, and an age that can never be cleared is not a hold condition.
    bool horizon_full();
    void refresh_ack_age();
    void rewire(const Wiring &p_wiring);
    void latch_quarantine(
        int64_t p_transition,
        bool p_stream_reconstructed,
        Attribution p_attribution,
        bool p_demoted = false
    );

private:
    // Re-keys a TICK tape when its clock jumps past the contiguous lane.
    void prepare_tick_tape(int64_t p_tick);
    void author_tape_entry(int64_t p_label, bool p_fresh);
    void record_drive(
        int64_t p_transition,
        const Fold &p_fold,
        const Dictionary &p_topology,
        const StateStamp &p_pre
    );
    int measure_quantum();
    int transition_span(int64_t p_basis) const;
    void enter_quarantine(
        int64_t p_transition,
        bool p_stream_reconstructed,
        bool p_demoted = false
    );
};

} // namespace predict

} // namespace netw
