#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "netw/predict/carry.hpp"
#include "netw/predict/command_queue.hpp"
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
#include "netw/api/timeline.hpp"

namespace netw {

namespace predict {

constexpr int TAPE_HISTORY_LIMIT = 256;

constexpr int ACK_AGE_MAX = 64;

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
    godot::Dictionary input;
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
};

struct Slot {
    Config config;
    Wiring wiring;
    FieldCodec input_codec;
    ObjectPort port;
    int64_t order_key = -1;
    godot::LocalVector<uint8_t> owner_has_state;
    godot::LocalVector<uint8_t> owner_has_input;
    bool owner_solves = false;
    godot::Callable simulate;
    godot::Callable witness;
    godot::Callable corridor;
    godot::HashMap<godot::StringName, godot::Callable> sensors;
    godot::HashMap<godot::StringName, godot::Callable> carry_rules;
    Journal journal;
    Tape tape;
    CommandQueue commands;
    godot::Ref<NetwTimeline> timeline;
    godot::Ref<NetwTimeline> entry_history;
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
    CarryDirty carry_dirty;
    WitnessSummary last_witness;
    godot::Dictionary last_samples;
    godot::Dictionary pending_provenance;
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
    int64_t out_of_domain_until = -1;

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

    void adopt_timing(const Timing &p_timing);
    void adopt_timeline(const godot::Ref<NetwTimeline> &p_timeline);
    void reset_entry_history();
    void reset_tape(int64_t p_epoch);

    godot::LocalVector<ReplayEntry> replay_entries(int64_t p_basis) const;

    godot::Dictionary state_before(int64_t p_transition) const;

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
    StateVerdict compare(
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

} // namespace predict

} // namespace netw
