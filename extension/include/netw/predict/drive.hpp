#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/arrival.hpp"
#include "netw/predict/books.hpp"
#include "netw/predict/carry.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/episode.hpp"
#include "netw/predict/joint.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/quarantine.hpp"
#include "netw/predict/recovery.hpp"
#include "netw/predict/sensors.hpp"
#include "netw/predict/transition_book.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/prediction_core.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/store/column.hpp"
#include "netw/store/entity_ref.hpp"

namespace netw::predict {

constexpr int TAPE_HISTORY_LIMIT = 256;

constexpr int ACK_AGE_MAX = 64;

static_assert((ACK_AGE_MAX & (ACK_AGE_MAX - 1)) == 0);
static_assert(TransitionBook::DEPTH == uint32_t(TAPE_HISTORY_LIMIT));

constexpr uint32_t book_depth_for(int p_schedule) {
    return p_schedule == int(Schedule::FRAME) ? TransitionBook::DEPTH
                                              : uint32_t(ACK_AGE_MAX);
}

enum VerdictReason : int {
    VERDICT_REASON_NONE = 0,
    VERDICT_REASON_AWAITING_RECONSTRUCTION = 1,
    VERDICT_REASON_REALIGN_PENDING = 2,
    VERDICT_REASON_RESEED_IGNORED = 3,
    VERDICT_REASON_PROBATION_REQUARANTINE = 4,
    VERDICT_REASON_EVIDENCE_EXHAUSTED = 5,
    VERDICT_REASON_TRANSPORT_PENDING = 6,
    VERDICT_REASON_DISSIPATE_PENDING = 7,
    VERDICT_REASON_DISSIPATED = 8,
    VERDICT_REASON_WITNESS_DEFERRED = 9,
    VERDICT_REASON_DECLINED = 10,
};

class Tape {
    godot::LocalVector<int64_t> indices;
    godot::LocalVector<int64_t> entry_labels;
    godot::LocalVector<uint8_t> fresh;
    int start = 0;
    int count = 0;

public:
    Tape();

    void append(int64_t p_index, int64_t p_label, bool p_fresh);
    void clear();

    int size() const {
        return count;
    }

    int64_t held_bytes() const {
        return held::vector(indices) + held::vector(entry_labels)
            + held::vector(fresh);
    }

    int64_t oldest_index() const;
    int64_t newest_index() const;

    int64_t label_of(int64_t p_index) const;
    bool is_fresh(int64_t p_index) const;
};

struct Timing {
    int64_t tick = 0;
    int64_t frame = 0;
    double delta = 0.0;
    double ticktime = 0.0;
    int quantum = 1;
    bool simulating = true;
};

struct StateStamp {
    int32_t fp = 0;
    FamilyFingerprints families;
    int32_t raw_fp = 0;
    uint8_t evidence_mask = 0;
};

struct DriveRecord {
    int64_t transition = -1;
    int64_t label = -1;
    DriveKind kind = DriveKind::NONE;
    bool ran = false;
    bool fresh = false;
    bool held = false;
    bool clamped = false;
};

struct ReplayEntry {
    int64_t index = -1;
    int64_t label = -1;
    int64_t input_key = -1;
};

struct ConsumeInputPlan {
    bool eligible = false;
    bool missing = false;
    bool run = false;
    bool use_last = false;
    DriveKind kind = DriveKind::NONE;
};

TriggerShape trigger_shape(
    const Wiring &p_wiring,
    const godot::LocalVector<double> &p_field_errors,
    double p_fallback_epsilon
);

ConsumeInputPlan plan_consume_input(
    Schedule p_schedule,
    bool p_has_input,
    bool p_has_later_input,
    bool p_has_last_input,
    MissingInput p_missing_policy
);

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
    int max_replay_depth = 0;
};

struct FieldReadings {
    godot::LocalVector<double> value;
    godot::LocalVector<uint8_t> present;

    void resize(int p_count);
    void clear();
    void note(int p_field, double p_value);
    bool has(int p_field) const;
    double at(int p_field, double p_absent) const;
    bool empty() const;
};

struct PassCursor {
    int64_t latest_input_tick = -1;
    int64_t last_driven_input_tick = -1;
    int64_t last_frame_transition_tick = -1;
    int64_t last_recorded_input_tick = -1;
    bool raw_fingerprints = false;
};

struct LaneCursor {
    int64_t last_recorded_entry_index = -1;
    int64_t replay_cursor = -1;
    int64_t last_replayed_label = -1;
    bool last_replayed_fresh = false;
    int64_t next_input_tick = -1;
    int64_t ack = -1;
    bool ack_advanced = false;
    int64_t ack_of_acks = -1;
    bool ack_domain_confirmed = true;
    int64_t owner_ack_floor = -1;
    int64_t command_epoch = -1;
    int64_t relayed_epoch = -1;
    int64_t newest_matrix_transition = -1;
    int arrivals_this_frame = 0;
    int64_t cooldown_until_tick = -1;
};

struct PayloadRows {
    godot::Dictionary open_topology;
};

struct RecoveryRows {
    StateRow before;
    StateRow write;
    StateRow carried;
    StateRow live;
    StateRow deltas;
    godot::LocalVector<int> projection;
    godot::LocalVector<double> tier_errors;
    RecoveryRequest request;
    RecoveryRequest effective;
    WritePlan answer;
    WritePlan staged;
    wire::CodeRow scratch;
    godot::PackedInt64Array codes;
    godot::LocalVector<godot::StringName> install_keys;
    godot::LocalVector<godot::Variant> install_values;
    godot::LocalVector<godot::Variant> install_target;
    godot::LocalVector<godot::StringName> moved_keys;
    godot::LocalVector<godot::Variant> moved;
};

struct PendingProvenance {
    int64_t episode = 0;
    int64_t write_id = 0;
    int64_t op = 0;
    int64_t basis = -1;
    bool held = false;
};

struct EntityRoster {
    godot::LocalVector<godot::ObjectID> members;

    int index_of(const godot::ObjectID &p_id) const;
    bool add(const godot::ObjectID &p_id);
    bool erase(const godot::ObjectID &p_id);
    void clear();

    int count() const {
        return int(members.size());
    }
};

struct DeclaredAxes {
    bool authority = false;
    bool controlled_locally = false;
    bool inputless = false;
    int64_t epoch = -1;
};

struct EngineLatches {
    bool registered = false;
    bool last_correction_teleported = false;
    int64_t validated_class_hash = 0;
    bool stream_reconstructed = false;
    bool fallback_latched = false;
    bool previous_witness_sleeping = false;
    bool has_previous_witness = false;
    bool invalid_witness_reported = false;
    bool invalid_command_predictor_reported = false;
    bool joint_refusal_reported = false;
    bool stepper_absence_reported = false;
    bool integration_refusal_reported = false;
    bool island_gap_reported = false;
    bool island_roster_seeded = false;
    int64_t joint_basis = -1;
    int64_t joint_relay_floor = -1;
    int64_t joint_epoch_floor = -1;
    int64_t tenure_begin = -1;
    int64_t tenure_end = -1;
};

struct RecoveryLedger {
    godot::LocalVector<int> triggered;
    godot::LocalVector<int> repaired;
    godot::LocalVector<int> contracted;
    godot::LocalVector<uint8_t> seeded;

    void resize(int p_count);
    void seed(int p_field);
    bool has(int p_field) const;
    void bump_triggered(int p_field);
    void bump_repaired(int p_field);
    void bump_contracted(int p_field);
};

struct ComparisonReport {
    int verdict_reason = 0;
    int attribution = 0;
    int64_t attributed_transition = -1;
    int compare_staleness = -1;
    bool reconciling = false;
    FieldReadings divergence;
    FieldReadings tier_error;
};

#if defined(DEBUG_ENABLED)
struct EvaluationRecord {
    int64_t recv_tick = -1;
    double divergence = 0.0;
    bool corrected = false;
    uint64_t serial = 0;
};
#endif

struct RelayWindow {
    static constexpr uint32_t SIZE = 4;

    int64_t ticks[SIZE] = {-1, -1, -1, -1};
    StateRow commands[SIZE];
    uint32_t count = 0;
    uint32_t next = 0;

    void record(int64_t p_tick, const StateRow &p_command);
    uint32_t at_oldest(uint32_t p_offset) const;
};

struct Slot {
    Config config;
    Wiring wiring;
    FieldCodec input_codec;
    ObjectPort port;
    int64_t order_key = -1;
    godot::LocalVector<uint8_t> owner_has_state;
    godot::LocalVector<uint8_t> owner_has_input;
    godot::LocalVector<store::Column> state_ports;
    godot::LocalVector<store::Column> input_ports;
    StateRow pulled;
    bool owner_solves = false;
    godot::Callable simulate;
    godot::Callable witness;
    godot::Callable corridor;
    godot::HashMap<godot::StringName, godot::Callable> sensors;
    godot::HashMap<godot::StringName, godot::Callable> carry_rules;
    Journal journal;
    Tape tape;
    ClaimBook owner_claims;
    WitnessClassBook authority_witness_classes;
    ContractionLedger contraction_ledger;
    WitnessDetailBook witness_details;
    DeferredOperator deferred_operator;
    godot::PackedStringArray island_participants;
    int64_t episode_revision = 0;
    TransitionBook inputs;
    int64_t input_floor = -1;
    bool lane_open = false;
    TransitionBook book;
    DriveStats stats;
    ComparisonReport report;
    RecoveryLedger recovery_ledger;
    PassCursor cursor;
    LaneCursor lane_cursor;
    EngineLatches latches;
    DeclaredAxes axes;
    EntityRoster rosters[4];
    PayloadRows rows;
    RecoveryRows recovering;
    uint64_t state_binding_id = 0;
    uint64_t input_binding_id = 0;
    store::EntityRef store;
    Listener state_listener = Listener::NONE;
    Listener input_listener = Listener::NONE;
    godot::ObjectID arrival_seat;
    godot::StringName breach_source;
    CompareStats compare_stats;
    StateVerdict last_state_verdict;
    StateRow compared;
    StateRow received;
    StateRow arrived;
    StateRow command;
    StateRow frame_command;
    StateRow stall;
    StateRow last;
    StateRow live;
    godot::Dictionary topology_fp_facts;
    int topology_fp_quantum = -1;
    int32_t topology_fp_value = 0;
    int topology_schedule = -1;
    int64_t topology_epoch = -1;
    godot::PackedStringArray topology_participants;
    bool topology_open = false;
    wire::CodeRow command_scratch;
    godot::PackedInt64Array command_codes;
    godot::LocalVector<double> correction_tolerances;
    godot::LocalVector<double> meter_tolerances;
    godot::LocalVector<double> judged_errors;
    wire::CodeRow fingerprint_scratch;
    StateRow state;
    Episode episode;
    sim::Selection joint_roster;
    int reconcile_own = 0;
    bool reconcile_joined = false;
    JointTrack joint;
    JointStats joint_stats;
    Quarantine quarantine;
    CarryTrack carry;
    CarryDirty carry_dirty;
    godot::LocalVector<CarryAttempt> carry_attempts;
    WitnessSummary last_witness;
    godot::Dictionary last_samples;
    PendingProvenance pending_provenance;
    RecoveryState recovery;
    RecoveryStats recovery_stats;
    WritePlan last_write_plan;
#if defined(DEBUG_ENABLED)
    EvaluationRecord last_evaluation;
#endif

    int64_t tape_epoch = 0;
    int64_t next_tape_entry_index = 0;
    int64_t last_driven_entry_index = -1;
    int64_t written_through = -1;
    bool owner_claim_mismatched = false;

    int64_t recorded_input_tick = -1;
    int64_t drive_input_tick = -1;
    int64_t drive_frame_tick = -1;
    int64_t latest_authority_ack = -1;
    int64_t ack_origin = -1;
    int64_t out_of_domain_until = -1;
    int64_t environment_epoch = -1;

    int64_t frame_index = 0;
    int64_t last_drive_frame = -1;
    int last_quantum = 1;
    int declared_quantum = 1;
    double tick_delta = 1.0 / 60.0;

    int32_t pending_c_hash = 0;
    bool witnessed_sleeping = false;
    bool witnessed_before = false;
    bool open = false;
    bool quantum_declaration_warned = false;
    bool relay_subscribed = false;
    RelayWindow relay_window;

    void adopt_timing(const Timing &p_timing);
    void open_lane();
    void close_lane();
    void forget_commands();
    void reset_tape(int64_t p_epoch);

    godot::LocalVector<ReplayEntry> replay_entries(int64_t p_basis) const;

    int64_t label_for(int64_t p_key) const;
    bool record_state_row(int64_t p_key, const StateRow &p_state);
    bool joint_state(int64_t p_transition, StateRow &r_state) const;
    int64_t joint_newest_state(int64_t p_transition) const;
    int64_t joint_oldest_state() const;
    void clear_joint_cells();

    int32_t topology_fp(const godot::Dictionary &p_topology);
    bool record_authored_row(
        int64_t p_key,
        int64_t p_label,
        bool p_fresh,
        const StateRow &p_input
    );
    void unacknowledged_row(int64_t p_key, StateRow &r_input) const;
    void authored_row(int64_t p_key, StateRow &r_input) const;
    bool holds_authored(int64_t p_key) const;
    bool has_authored(int64_t p_key) const;
    int64_t newest_authored() const;
    void authored_ticks(
        int64_t p_from,
        int64_t p_to,
        godot::LocalVector<int64_t> &r_ticks
    ) const;
    bool admit_command(
        int64_t p_key,
        int64_t p_label,
        bool p_fresh,
        const StateRow &p_command
    );
    bool record_joint_command(
        int64_t p_key,
        const StateRow &p_command,
        CellProvenance p_provenance
    );
    bool holds_command(int64_t p_key) const;
    int command_depth_from(int64_t p_cursor) const;
    godot::PackedInt64Array command_keys() const;

    void mark_carry_dirty(int64_t p_transition);
    bool carry_judgeable(int64_t p_transition) const;
    void record_input(int64_t p_tick, int32_t p_c_hash);

    void trim_history(int64_t p_ack);

    DriveRecord open_drive(
        const Timing &p_timing,
        const godot::Dictionary &p_topology,
        const StateStamp &p_pre
    );

    DriveRecord replay_drive(
        const Timing &p_timing,
        const godot::Dictionary &p_topology,
        int64_t p_transition,
        int64_t p_label,
        DriveKind p_kind,
        const StateStamp &p_pre,
        bool p_authoring
    );

    bool record_evidence(
        int64_t p_transition,
        int64_t p_environment_epoch,
        const godot::Dictionary &p_environment,
        const godot::Dictionary &p_topology,
        const godot::LocalVector<WitnessContact> &p_contacts,
        bool p_sleeping
    );
    void close_drive(int64_t p_transition, const StateStamp &p_post);

    void declare_skipped(int64_t p_transition, int64_t p_label);

    void acknowledge(int64_t p_transition, bool p_matched);
    AckVerdict admit_ack(
        int64_t p_transition,
        const EvidenceRow &p_peer,
        bool p_substituted
    );
    const StateVerdict &compare(
        int64_t p_recv_tick,
        int64_t p_transition,
        const StateRow &p_predicted,
        const StateRow &p_authority,
        const godot::LocalVector<double> &p_correction_tolerances,
        const godot::LocalVector<double> &p_meter_tolerances,
        double p_fallback_epsilon,
        bool p_stream_reconstructed,
        bool p_ack_domain_confirmed
    );
    void set_state(const StateRow &p_state);
    const WritePlan &recover(const RecoveryRequest &p_request);
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
    void adopt_alignment(int64_t p_transition, int64_t p_ignore_through);
    bool finish_probation(bool p_corrected);
    void open_recovery_window(int64_t p_label, int p_cooldown);
    void suppress_recovery_until(int64_t p_label, int p_cooldown);

    bool horizon_full();
    void refresh_ack_age();
    void rewire(const Wiring &p_wiring);
    void record_idle_drive(int64_t p_label, DriveKind p_kind);
    void record_authoring_clamp();
    void record_speculation_hold();
    void mark_authority_ack(int64_t p_transition);
    void latch_quarantine(
        int64_t p_transition,
        bool p_stream_reconstructed,
        Attribution p_attribution,
        bool p_demoted = false
    );

    void prepare_tick_tape(int64_t p_tick);
    void author_tape_entry(int64_t p_label, bool p_fresh);
    int transition_span(int64_t p_basis) const;

private:
    void record_drive(
        int64_t p_transition,
        const Fold &p_fold,
        const godot::Dictionary &p_topology,
        const StateStamp &p_pre
    );
    int measure_quantum();
    void enter_quarantine(
        int64_t p_transition,
        bool p_stream_reconstructed,
        bool p_demoted = false
    );
};

} // namespace netw::predict
