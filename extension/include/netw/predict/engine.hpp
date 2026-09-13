#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/predict/drive.hpp"
#include "netw/predict/episode_report.hpp"
#include "netw/predict/feed.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/prediction_core.hpp"

namespace netw {

using table::SchemaRecord;

class NetwMultiplayer;
class NetwPredictIsland;
class NetwTimeline;
class NetwPredictionHandle;
class NetwPredictStats;
class NetwPropertySetBinding;

predict::EvidenceRow evidence_row(
    int64_t p_pre_fp,
    int64_t p_c_hash,
    int64_t p_e_digest,
    int64_t p_post_fp,
    int64_t p_pre_pose_fp,
    int64_t p_pre_momentum_fp,
    int64_t p_pre_controller_fp,
    int64_t p_post_pose_fp,
    int64_t p_post_momentum_fp,
    int64_t p_post_controller_fp,
    int64_t p_topo_fp,
    int64_t p_witness_fp,
    int64_t p_raw_fp,
    int p_evidence_mask,
    bool p_complete
);

predict::RecoveryRequest recovery_request(
    int p_width,
    const godot::Array &p_predicted,
    const godot::Array &p_authority,
    const godot::Array &p_current,
    const godot::PackedFloat64Array &p_field_errors,
    int64_t p_basis,
    int64_t p_current_label,
    int p_policy,
    double p_fallback_epsilon,
    double p_fallback_teleport,
    int p_max_restore_ticks,
    int p_ack_age_ticks,
    int p_collision_cooldown_ticks,
    double p_tick_delta,
    int p_domain,
    int p_attribution,
    bool p_contact_window,
    bool p_suppressed,
    bool p_pose_unmeasured
);

predict::FieldDecl field_decl(
    const godot::StringName &p_key,
    int p_property_class,
    const godot::StringName &p_carry_channel = godot::StringName(),
    double p_converge_stiffness = 0.0,
    bool p_teleport_only = false,
    bool p_reconcile_only = false,
    double p_epsilon_override = -1.0,
    double p_teleport_at = -1.0,
    bool p_angle = false,
    const godot::Ref<NetwQuantize> &p_quantizer = godot::Ref<NetwQuantize>(),
    int p_type = int(godot::Variant::NIL)
);

class NetwPredictTiming {
    friend class NetwPredictionEngine;

    predict::Timing carried;

public:
    static NetwPredictTiming of(
        int64_t p_tick,
        double p_delta,
        double p_ticktime,
        int64_t p_frame = 0,
        int p_quantum = 1,
        bool p_simulating = true
    );

    int64_t get_tick() const {
        return carried.tick;
    }

    double get_delta() const {
        return carried.delta;
    }

    double get_ticktime() const {
        return carried.ticktime;
    }

    int64_t get_frame() const {
        return carried.frame;
    }

    int get_quantum() const {
        return carried.quantum;
    }

    bool get_simulating() const {
        return carried.simulating;
    }
};

class NetwPredictCarryContext : public godot::RefCounted {
    GDCLASS(NetwPredictCarryContext, godot::RefCounted)

    friend class NetwPredictionEngine;

    godot::Dictionary state_at;
    godot::Dictionary input_at;
    double step_delta = 0.0;
    int64_t authored_label = -1;

protected:
    static void _bind_methods();

public:
    godot::Dictionary get_state() const {
        return state_at;
    }

    godot::Dictionary get_input() const {
        return input_at;
    }

    double get_delta() const {
        return step_delta;
    }

    int64_t get_label() const {
        return authored_label;
    }
};

class NetwPredictionEngine {
    godot::HashMap<int64_t, predict::Slot> rows;
    godot::HashMap<uint64_t, int64_t> slot_by_entity;
    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> entity_by_slot;
    godot::ObjectID core_id;
    int64_t quantum_reported = -1;
    int64_t next_slot = 1;
    int depth = 0;
    int64_t mutations_refused = 0;

    bool roster_open(const char *p_verb);

    NetwMultiplayer *core() const;

    godot::Ref<NetwEntity> owner_entity(int64_t p_slot) const;
    godot::Object *handle_of(int64_t p_slot) const;
    static godot::StringName episode_edge(int64_t p_event);

    void open_tenure(
        int64_t p_slot,
        const godot::Ref<NetwEntity> &p_member,
        int64_t p_transition
    );
    void close_tenure(
        int64_t p_slot,
        const godot::Ref<NetwEntity> &p_member,
        int64_t p_transition
    );

    const predict::Slot *row_of(int64_t p_slot) const;
    predict::Slot *mutable_row_of(int64_t p_slot);

    static godot::Object *resolve_owner(predict::Slot &p_row);
    static void resolve_reach(
        predict::Slot &p_row,
        const godot::Object *p_owner
    );
    static int32_t compared_fingerprint(
        predict::Slot &p_row,
        const godot::Dictionary &p_payload
    );
    godot::Variant call_carry(
        int64_t p_slot,
        const godot::Callable &p_rule,
        const godot::Variant &p_value,
        const predict::ReplayEntry &p_entry,
        const godot::Dictionary &p_state
    );
    void replay_carry(
        predict::CarryAttempt &r_attempt,
        int64_t p_slot,
        const godot::Callable &p_rule,
        const godot::StringName &p_field,
        int p_field_slot,
        const godot::LocalVector<predict::ReplayEntry> &p_entries,
        bool p_angle,
        double p_divergence_epsilon
    );
    void fold_carry(
        predict::CarryAttempt &r_attempt,
        int64_t p_slot,
        const godot::Callable &p_rule,
        int p_field_slot,
        const godot::Variant &p_start,
        const godot::LocalVector<predict::ReplayEntry> &p_entries,
        bool p_angle,
        double p_teleport_default
    );
    static godot::Dictionary capture_through(
        predict::Slot &p_row,
        const predict::FieldCodec &p_codec,
        const godot::LocalVector<uint8_t> &p_reach
    );
    static bool apply_through(
        predict::Slot &p_row,
        const predict::FieldCodec &p_codec,
        const godot::LocalVector<uint8_t> &p_reach,
        const godot::Dictionary &p_payload
    );

public:
    int64_t open(
        const godot::LocalVector<predict::FieldDecl> &p_declaration
        = godot::LocalVector<predict::FieldDecl>()
    );
    void rewire(
        int64_t p_slot,
        const godot::LocalVector<predict::FieldDecl> &p_declaration,
        const godot::LocalVector<predict::FieldDecl> &p_input
        = godot::LocalVector<predict::FieldDecl>()
    );
    void close(int64_t p_slot);

    int64_t current_tick() const;
    void report_quantum_declaration(int64_t p_slot);
    godot::Ref<NetwTimeline> register_timeline(
        const godot::Ref<NetwEntity> &p_entity
    );

    int adopt_declaration(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::Ref<NetwPropertySetBinding> &p_state,
        const godot::Ref<NetwPropertySetBinding> &p_input,
        int p_schedule,
        int p_role,
        int p_declared_correction,
        int p_restore,
        int p_max_restore_ticks,
        int p_island,
        bool p_witness
    );

    int64_t slot_register(const godot::Ref<godot::RefCounted> &p_entity);
    int64_t slot_of(const godot::Ref<godot::RefCounted> &p_entity) const;
    void slot_unregister(const godot::Ref<godot::RefCounted> &p_entity);
    int64_t slot_registered() const;
    bool slot_bind_owner(
        const godot::Ref<godot::RefCounted> &p_entity,
        godot::Object *p_owner
    );
    void slot_unbind_owner(const godot::Ref<godot::RefCounted> &p_entity);

    bool bind_owner(int64_t p_slot, godot::Object *p_owner);
    void unbind_owner(int64_t p_slot);
    bool owner_bound(int64_t p_slot) const;
    bool owner_solves(int64_t p_slot) const;

    godot::Dictionary capture_state(int64_t p_slot);
    godot::Dictionary capture_input(int64_t p_slot);
    godot::Dictionary author_input(int64_t p_slot, int64_t p_tick);
    bool apply_state(int64_t p_slot, const godot::Dictionary &p_payload);
    bool apply_input(int64_t p_slot, const godot::Dictionary &p_payload);

    void set_simulate(int64_t p_slot, const godot::Callable &p_callable);
    void set_witness(int64_t p_slot, const godot::Callable &p_callable);
    void set_corridor(int64_t p_slot, const godot::Callable &p_callable);
    void set_sensor(
        int64_t p_slot,
        const godot::StringName &p_name,
        const godot::Callable &p_callable
    );
    bool is_angle_field(int64_t p_slot, const godot::StringName &p_key) const;
    godot::Dictionary angle_fields_of(int64_t p_slot) const;
    bool is_causal_field(int64_t p_slot, const godot::StringName &p_key) const;
    bool is_trigger_excluded_field(
        int64_t p_slot,
        const godot::StringName &p_key
    ) const;
    double epsilon_for_field(
        int64_t p_slot,
        const godot::StringName &p_key,
        double p_fallback
    ) const;
    godot::Dictionary compared_state_of(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;
    godot::Dictionary projection_map(int64_t p_slot) const;
    godot::Dictionary wiring_snapshot(int64_t p_slot) const;

    godot::Dictionary teleport_distances(int64_t p_slot) const;
    bool teleport_reached_of(
        int64_t p_slot,
        const godot::Dictionary &p_pose_errors,
        double p_fallback
    ) const;

    godot::Dictionary transport_of(
        int64_t p_slot,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authority,
        const godot::Dictionary &p_current
    ) const;

    godot::Dictionary frame_input_of(int64_t p_slot) const;
    void set_frame_input(int64_t p_slot, const godot::Dictionary &p_row);
    godot::Dictionary stall_input_of(int64_t p_slot) const;
    void set_stall_input(int64_t p_slot, const godot::Dictionary &p_row);
    godot::Dictionary last_input_of(int64_t p_slot) const;
    void set_last_input(int64_t p_slot, const godot::Dictionary &p_row);
    godot::Dictionary open_topology_of(int64_t p_slot) const;
    void set_open_topology(int64_t p_slot, const godot::Dictionary &p_row);

    enum Roster {
        ROSTER_ISLAND_MEMBERS = 0,
        ROSTER_SIMULATED = 1,
        ROSTER_JOINT_LINGERING = 2,
        ROSTER_REALIZED_CONTACT = 3,
    };

    godot::TypedArray<NetwEntity> roster_list(int64_t p_slot, int p_roster);
    bool roster_has(
        int64_t p_slot,
        int p_roster,
        const godot::Ref<NetwEntity> &p_entity
    ) const;
    bool roster_add(
        int64_t p_slot,
        int p_roster,
        const godot::Ref<NetwEntity> &p_entity
    );
    bool roster_erase(
        int64_t p_slot,
        int p_roster,
        const godot::Ref<NetwEntity> &p_entity
    );
    void roster_clear(int64_t p_slot, int p_roster);
    int roster_count(int64_t p_slot, int p_roster) const;
    void roster_assign(
        int64_t p_slot,
        int p_roster,
        const godot::TypedArray<NetwEntity> &p_members
    );

    bool registered_of(int64_t p_slot) const;
    void set_registered(int64_t p_slot, bool p_value);
    bool last_correction_teleported_of(int64_t p_slot) const;
    void set_last_correction_teleported(int64_t p_slot, bool p_value);
    int64_t validated_class_hash_of(int64_t p_slot) const;
    void set_validated_class_hash(int64_t p_slot, int64_t p_value);
    bool stream_reconstructed_of(int64_t p_slot) const;
    void set_stream_reconstructed(int64_t p_slot, bool p_value);
    bool fallback_latched_of(int64_t p_slot) const;
    void set_fallback_latched(int64_t p_slot, bool p_value);
    bool previous_witness_sleeping_of(int64_t p_slot) const;
    void set_previous_witness_sleeping(int64_t p_slot, bool p_value);
    bool has_previous_witness_of(int64_t p_slot) const;
    void set_has_previous_witness(int64_t p_slot, bool p_value);
    bool invalid_witness_reported_of(int64_t p_slot) const;
    void set_invalid_witness_reported(int64_t p_slot, bool p_value);
    godot::Dictionary witness_sample(
        int64_t p_slot,
        const godot::Ref<NetwPredictionHandle> &p_handle
    );
    godot::Dictionary normalize_witness_sample(
        int64_t p_slot,
        const godot::Variant &p_value
    );
    godot::Dictionary predicted_command(
        int64_t p_slot,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_tick
    );
    bool invalid_command_predictor_reported_of(int64_t p_slot) const;
    void set_invalid_command_predictor_reported(int64_t p_slot, bool p_value);
    bool joint_refusal_reported_of(int64_t p_slot) const;
    void set_joint_refusal_reported(int64_t p_slot, bool p_value);
    bool stepper_absence_reported_of(int64_t p_slot) const;
    void set_stepper_absence_reported(int64_t p_slot, bool p_value);
    int resolved_schedule(int64_t p_slot, int p_declared);
    bool island_gap_reported_of(int64_t p_slot) const;
    void set_island_gap_reported(int64_t p_slot, bool p_value);
    bool island_roster_seeded_of(int64_t p_slot) const;
    void set_island_roster_seeded(int64_t p_slot, bool p_value);
    int64_t joint_basis_of(int64_t p_slot) const;
    void set_joint_basis(int64_t p_slot, int64_t p_value);
    int64_t joint_relay_floor_of(int64_t p_slot) const;
    void set_joint_relay_floor(int64_t p_slot, int64_t p_value);
    int64_t joint_epoch_floor_of(int64_t p_slot) const;
    void set_joint_epoch_floor(int64_t p_slot, int64_t p_value);
    int64_t tenure_begin_of(int64_t p_slot) const;
    void set_tenure_begin(int64_t p_slot, int64_t p_value);
    int64_t tenure_end_of(int64_t p_slot) const;
    void set_tenure_end(int64_t p_slot, int64_t p_value);

    int64_t tape_epoch_of(int64_t p_slot) const;
    void set_tape_epoch(int64_t p_slot, int64_t p_value);
    int64_t next_tape_entry_index_of(int64_t p_slot) const;
    void set_next_tape_entry_index(int64_t p_slot, int64_t p_value);
    int64_t last_driven_entry_index_of(int64_t p_slot) const;
    void set_last_driven_entry_index(int64_t p_slot, int64_t p_value);
    int64_t last_recorded_entry_index_of(int64_t p_slot) const;
    void set_last_recorded_entry_index(int64_t p_slot, int64_t p_value);
    int64_t replay_cursor_of(int64_t p_slot) const;
    void set_replay_cursor(int64_t p_slot, int64_t p_value);
    int64_t last_replayed_label_of(int64_t p_slot) const;
    void set_last_replayed_label(int64_t p_slot, int64_t p_value);
    bool last_replayed_fresh_of(int64_t p_slot) const;
    void set_last_replayed_fresh(int64_t p_slot, bool p_value);
    int64_t next_input_tick_of(int64_t p_slot) const;
    void set_next_input_tick(int64_t p_slot, int64_t p_value);
    int64_t ack_of(int64_t p_slot) const;
    void set_ack(int64_t p_slot, int64_t p_value);
    bool ack_advanced_of(int64_t p_slot) const;
    void set_ack_advanced(int64_t p_slot, bool p_value);
    int64_t ack_of_acks_of(int64_t p_slot) const;
    void set_ack_of_acks(int64_t p_slot, int64_t p_value);
    bool ack_domain_confirmed_of(int64_t p_slot) const;
    void set_ack_domain_confirmed(int64_t p_slot, bool p_value);
    int64_t owner_ack_floor_of(int64_t p_slot) const;
    void set_owner_ack_floor(int64_t p_slot, int64_t p_value);
    int64_t command_epoch_of(int64_t p_slot) const;
    void set_command_epoch(int64_t p_slot, int64_t p_value);
    int64_t relayed_epoch_of(int64_t p_slot) const;
    void set_relayed_epoch(int64_t p_slot, int64_t p_value);
    int64_t newest_matrix_transition_of(int64_t p_slot) const;
    void set_newest_matrix_transition(int64_t p_slot, int64_t p_value);
    int arrivals_this_frame_of(int64_t p_slot) const;
    void set_arrivals_this_frame(int64_t p_slot, int p_value);
    int64_t cooldown_until_tick_of(int64_t p_slot) const;
    void set_cooldown_until_tick(int64_t p_slot, int64_t p_value);

    double tick_delta_of(int64_t p_slot) const;
    void set_tick_delta(int64_t p_slot, double p_value);
    int64_t frame_index_of(int64_t p_slot) const;
    void set_frame_index(int64_t p_slot, int64_t p_value);
    int declared_quantum_of(int64_t p_slot) const;
    void set_declared_quantum(int64_t p_slot, int p_value);
    int64_t latest_input_tick_of(int64_t p_slot) const;
    void set_latest_input_tick(int64_t p_slot, int64_t p_value);
    int64_t last_driven_input_tick_of(int64_t p_slot) const;
    void set_last_driven_input_tick(int64_t p_slot, int64_t p_value);
    int64_t last_frame_transition_tick_of(int64_t p_slot) const;
    void set_last_frame_transition_tick(int64_t p_slot, int64_t p_value);
    int64_t last_recorded_input_tick_of(int64_t p_slot) const;
    void set_last_recorded_input_tick(int64_t p_slot, int64_t p_value);
    bool raw_fingerprints_of(int64_t p_slot) const;
    void set_raw_fingerprints(int64_t p_slot, bool p_value);

    void seed_recovery_ledger(int64_t p_slot);
    godot::TypedArray<godot::Dictionary> tape_transitions(int64_t p_slot) const;
    godot::Dictionary command_cell_at(
        int64_t p_slot,
        int64_t p_transition
    ) const;
    godot::PackedFloat64Array divergence_columns(int64_t p_slot) const;
    void ledger_note_comparison(
        int64_t p_slot,
        bool p_corrected,
        int64_t p_ack,
        double p_divergence_epsilon
    );
    void ledger_seed(int64_t p_slot, const godot::StringName &p_key);
    void ledger_bump_triggered(int64_t p_slot, const godot::StringName &p_key);
    void ledger_bump_repaired(int64_t p_slot, const godot::StringName &p_key);
    void ledger_bump_contracted(int64_t p_slot, const godot::StringName &p_key);
    godot::PackedInt64Array ledger_counts(
        int64_t p_slot,
        const godot::StringName &p_key
    ) const;
    godot::Array ledger_fields(int64_t p_slot) const;
    godot::Dictionary field_recovery(int64_t p_slot);

    bool note_simulated_by(
        int64_t p_slot,
        int64_t p_subject,
        const godot::Callable &p_predictor
    );
    bool clear_simulated_by(int64_t p_slot, int64_t p_subject);
    void clear_simulation_subjects(int64_t p_slot);
    int simulation_subject_count(int64_t p_slot) const;
    godot::Callable first_simulation_predictor(int64_t p_slot) const;

    void note_breach_source(int64_t p_slot, const godot::StringName &p_source);
    godot::StringName breach_source_of(int64_t p_slot) const;

    void note_divergence(int64_t p_slot, const godot::Dictionary &p_by_field);
    void clear_divergence(int64_t p_slot);
    godot::Dictionary divergence_report(int64_t p_slot) const;
    double divergence_of(
        int64_t p_slot,
        const godot::StringName &p_key,
        double p_absent
    ) const;
    bool has_divergence(int64_t p_slot, const godot::StringName &p_key) const;

    void note_tier_errors(int64_t p_slot, const godot::Dictionary &p_by_field);
    void clear_tier_errors(int64_t p_slot);
    godot::Dictionary tier_error_report(int64_t p_slot) const;

    void note_verdict_reason(int64_t p_slot, int p_reason);
    int verdict_reason_of(int64_t p_slot) const;
    void note_attribution(
        int64_t p_slot,
        int p_attribution,
        int64_t p_transition
    );
    int attribution_of(int64_t p_slot) const;
    int64_t attributed_transition_of(int64_t p_slot) const;
    void note_compare_staleness(int64_t p_slot, int p_ticks);
    int compare_staleness_of(int64_t p_slot) const;
    void note_reconciling(int64_t p_slot, bool p_value);
    bool reconciling(int64_t p_slot) const;

    int carry_rule_count(int64_t p_slot) const;
    bool has_carry_rule(int64_t p_slot, const godot::StringName &p_field) const;
    godot::Array carry_rule_fields(int64_t p_slot) const;

    void push_carry_rules(int64_t p_slot);
    predict::Feed role_feed(
        int p_role,
        bool p_fallback_latched,
        const godot::Callable &p_on_state,
        const godot::Callable &p_on_input,
        const godot::Callable &p_on_simulated,
        const godot::Callable &p_on_quarantine
    ) const;
    void set_carry(
        int64_t p_slot,
        const godot::StringName &p_field,
        const godot::Callable &p_callable
    );
    bool has_simulate(int64_t p_slot) const;

    void set_order_key(int64_t p_slot, int64_t p_order_key);
    int64_t order_key_of(int64_t p_slot) const;
    godot::PackedInt64Array ordered_slots() const;

    enum PassPhase {
        PASS_ISLAND_TICK = 0,
        PASS_ISLAND_FRAME = 1,
        PASS_JOINT = 2,
        PASS_TICK = 3,
        PASS_FRAME = 4,
        PASS_FINALIZE_FRAME = 5,
        PASS_STEPPED = 6,
    };

    godot::PackedInt64Array pass_slots(int p_phase) const;

    godot::Dictionary run_step(
        int64_t p_slot,
        const godot::Dictionary &p_input,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );

    int pass_depth() const;
    int64_t mutations_refused_count() const;

    godot::Dictionary canonicalize_state(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;
    godot::Dictionary canonicalize_input(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;
    godot::Dictionary coast_command(int64_t p_slot) const;

    int64_t sample_environment(int64_t p_slot, int64_t p_epoch);
    godot::Dictionary sensor_samples(int64_t p_slot) const;
    godot::PackedByteArray canonical_state_bytes(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;
    godot::PackedByteArray canonical_input_bytes(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;

    bool is_open(int64_t p_slot) const;
    int open_count() const;

    bool supports(
        int p_schedule,
        int p_role,
        int p_correction,
        int p_restore,
        int p_island = 0
    ) const;

    static godot::Dictionary archetype_axes(int p_archetype);
    static int role_for_axes(int p_input_source, int p_sim_mode);
    static int correction_for_recovery_policy(int p_policy);

    bool reconfigure_from(
        int64_t p_slot,
        const godot::Ref<NetwPredictionHandle> &p_handle
    );
    bool configure(
        int64_t p_slot,
        int p_schedule,
        int p_role,
        int p_correction,
        int p_restore,
        int p_max_restore_ticks = 6,
        int p_island = 0,
        bool p_witness = false,
        bool p_island_declared = false,
        bool p_island_approximate = false,
        double p_epsilon = 0.01,
        double p_teleport_threshold = 2.0,
        int p_collision_cooldown_ticks = 6
    );

    double epsilon_of_slot(int64_t p_slot) const;
    double teleport_threshold_of(int64_t p_slot) const;
    int collision_cooldown_of(int64_t p_slot) const;

    int schedule_of(int64_t p_slot) const;
    int role_of(int64_t p_slot) const;
    int correction_of(int64_t p_slot) const;
    void set_role(int64_t p_slot, int p_value);
    void set_correction(int64_t p_slot, int p_value);
    int island_of(int64_t p_slot) const;
    bool island_declared(int64_t p_slot) const;
    bool island_approximate(int64_t p_slot) const;

    int64_t out_of_domain_until(int64_t p_slot) const;
    bool out_of_domain_at(int64_t p_slot, int64_t p_label) const;
    void open_out_of_domain_window(
        int64_t p_slot,
        int64_t p_label,
        int p_cooldown
    );
    void clear_out_of_domain_window(int64_t p_slot);

    bool adopt_environment_epoch(
        int64_t p_slot,
        int64_t p_epoch,
        int64_t p_label,
        int p_cooldown
    );
    void clear_environment_epoch(int64_t p_slot);

    void record_owner_claims(
        int64_t p_slot,
        const predict::CommandFrameRecord &p_frame
    );
    void clear_owner_claims(int64_t p_slot);
    int owner_claim_count(int64_t p_slot) const;

    enum ClaimColumn {
        CLAIM_JUDGED = 0,
        CLAIM_MATCHED = 1,
        CLAIM_ATTRIBUTION = 2,
        CLAIM_COLUMN_COUNT = 3,
    };

    godot::PackedInt64Array judge_owner_claim(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_fingerprint
    );

    void record_authority_witness_class(
        int64_t p_slot,
        int64_t p_transition,
        int p_witness_class
    );
    int authority_witness_class(int64_t p_slot, int64_t p_transition) const;
    void clear_authority_witness_classes(int64_t p_slot);

    void ledger_arm(
        int64_t p_slot,
        int p_field,
        double p_error,
        int64_t p_basis
    );
    bool ledger_armed(int64_t p_slot) const;
    godot::PackedInt32Array ledger_settle(
        int64_t p_slot,
        int64_t p_ack,
        const godot::PackedFloat64Array &p_divergence
    );
    void ledger_clear(int64_t p_slot);

    godot::Dictionary pending_provenance(int64_t p_slot) const;
    void set_pending_provenance(
        int64_t p_slot,
        const godot::Dictionary &p_provenance
    );
    void stamp_pending_provenance(int64_t p_slot, int64_t p_transition);

    predict::ConsumeInputPlan plan_consume_input(
        int p_schedule,
        bool p_has_input,
        bool p_has_later_input,
        bool p_has_last_input,
        int p_missing_policy
    ) const;

    int field_count(int64_t p_slot) const;
    int field_slot(int64_t p_slot, const godot::StringName &p_key) const;
    godot::StringName field_name(int64_t p_slot, int p_field) const;

    int32_t state_fingerprint(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    );
    godot::PackedInt32Array state_family_fingerprints(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    );

    int projection_of(int64_t p_slot, int p_field) const;
    int state_family_of(int64_t p_slot, int p_field) const;
    double converge_rate_of(int64_t p_slot, int p_field) const;
    double epsilon_of(int64_t p_slot, int p_field) const;
    double teleport_of(int64_t p_slot, int p_field) const;
    bool is_pose(int64_t p_slot, int p_field) const;
    bool is_withheld(int64_t p_slot, int p_field) const;
    bool is_trigger_excluded(int64_t p_slot, int p_field) const;
    bool is_vote_excluded(int64_t p_slot, int p_field) const;
    bool is_angle(int64_t p_slot, int p_field) const;
    bool is_causal(int64_t p_slot, int p_field) const;

    void record_input(int64_t p_slot, int64_t p_tick, int64_t p_c_hash);

    predict::DriveRecord open_drive(
        int64_t p_slot,
        const godot::Dictionary &p_topology,
        int64_t p_tick,
        int64_t p_frame,
        double p_ticktime,
        int p_quantum,
        bool p_simulating,
        int64_t p_pre_fp,
        int64_t p_pre_pose_fp,
        int64_t p_pre_momentum_fp,
        int64_t p_pre_controller_fp,
        int64_t p_raw_fp = 0,
        int p_evidence_mask = 0
    );

    predict::DriveRecord replay_drive(
        int64_t p_slot,
        const godot::Dictionary &p_topology,
        int64_t p_transition,
        int64_t p_label,
        int p_kind,
        int64_t p_tick,
        int64_t p_frame,
        double p_ticktime,
        int p_quantum,
        int64_t p_pre_fp,
        int64_t p_pre_pose_fp,
        int64_t p_pre_momentum_fp,
        int64_t p_pre_controller_fp,
        int64_t p_raw_fp = 0,
        int p_evidence_mask = 0,
        bool p_authoring = false
    );

    bool record_evidence(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_environment_epoch,
        const godot::Dictionary &p_environment,
        const godot::Dictionary &p_topology,
        const godot::PackedStringArray &p_contact_ids,
        const godot::PackedInt32Array &p_witness_classes,
        const godot::PackedInt32Array &p_realizations,
        const godot::PackedByteArray &p_outside_boundary,
        bool p_sleeping
    );

    int resolve_axes(
        int64_t p_slot,
        const godot::Ref<NetwPredictionHandle> &p_handle
    );
    int resolve_correction(int64_t p_slot, int p_declared) const;

    void record_episode_decision(
        int64_t p_slot,
        int p_operator,
        int64_t p_basis,
        bool p_eligible,
        bool p_applied,
        const godot::Dictionary &p_eligibility
    );

    bool open_episode(int64_t p_slot, int64_t p_transition, int p_attribution);
    void record_episode_divergence(int64_t p_slot, int64_t p_transition);
    int escalation_field_of(
        int64_t p_slot,
        const godot::Array &p_predicted,
        const godot::Array &p_authority,
        const godot::PackedFloat64Array &p_field_errors,
        double p_fallback_epsilon
    ) const;
    int trigger_shape_of(
        int64_t p_slot,
        const godot::PackedFloat64Array &p_field_errors,
        double p_fallback_epsilon
    ) const;
    void record_episode_escalation(int64_t p_slot, int p_trigger_shape);
    void stamp_episode_write_delta(int64_t p_slot, int p_delta_fp);
    int record_episode_write(
        int64_t p_slot,
        int p_operator,
        int64_t p_basis,
        int p_delta_fp,
        const godot::StringName &p_target,
        int p_ack_age,
        int p_trigger_shape,
        bool p_evidence_free,
        bool p_null_operator
    );

    bool episode_active(int64_t p_slot) const;
    int episode_state(int64_t p_slot) const;

    enum EpisodeStat {
        STAT_EPISODE_ID = 0,
        STAT_EPISODE_STATE = 1,
        STAT_EPISODE_OPENED = 2,
        STAT_EPISODE_ATTRIBUTION = 3,
        STAT_EPISODE_NON_CONTRACTION = 4,
        STAT_EPISODE_WITHHELD_NC = 5,
        STAT_EPISODE_EVIDENCE_FREE_NC = 6,
        STAT_EPISODE_NO_TRIGGER_NC = 7,
        STAT_EPISODE_MIXED_TRIGGER_NC = 8,
        STAT_EPISODE_CLOSURE_USED = 9,
        STAT_EPISODE_AGREEMENT_RUN = 10,
        STAT_EPISODE_LAST_COMPARISON = 11,
        STAT_EPISODE_AGREEMENT_WRITE_ID = 12,
        STAT_EPISODE_LAST_WRITE_ID = 13,
        STAT_EPISODE_EVIDENCE_DROPPED = 14,
        STAT_EPISODE_WRITE_COUNT = 15,
        STAT_EPISODE_COMPARISON_COUNT = 16,
        STAT_EPISODE_ACTIVE = 17,
        STAT_EPISODE_TRANSPORT_DECIDED = 18,
        STAT_EPISODE_DISSIPATE_DECIDED = 19,
        STAT_EPISODE_COUNT = 20,
    };

    godot::PackedInt64Array episode_stats(int64_t p_slot) const;
    bool episode_budget_exhausted(int64_t p_slot) const;
    bool episode_operator_pending(int64_t p_slot, int p_operator) const;

    void record_breach(int64_t p_slot, int64_t p_transition);
    bool demote_for_breach(
        int64_t p_slot,
        int64_t p_transition,
        bool p_breached,
        int p_witness_fingerprint,
        const godot::Callable &p_send_command,
        const godot::Callable &p_enter_fallback
    );

    bool witness_row_clean(
        int64_t p_slot,
        int64_t p_transition,
        bool p_require_peer
    ) const;

    bool transport_admissible(
        int64_t p_slot,
        bool p_candidate,
        bool p_basis_witness_clean,
        bool p_recent_witness_clean,
        bool p_non_pose_agrees,
        bool p_below_teleport,
        bool p_escalated,
        bool p_observing
    ) const;
    bool dissipate_admissible(
        int64_t p_slot,
        bool p_momentum_active,
        bool p_other_active,
        bool p_basis_witness_clean,
        bool p_recent_witness_clean,
        bool p_escalated,
        bool p_observing,
        int p_meter
    ) const;

    bool dissipate_declared(int64_t p_slot) const;

    enum DissipateDecision {
        DISSIPATE_UNDECIDED = -1,
        DISSIPATE_DECLINED = 0,
        DISSIPATE_APPLIED = 1,
    };

    int try_dissipate(
        int64_t p_slot,
        int64_t p_basis,
        int p_meter,
        int p_domain,
        bool p_escalated,
        double p_divergence_epsilon,
        bool p_observing,
        const godot::StringName &p_target,
        int p_ack_age_ticks
    );

    bool static_geometry(godot::Object *p_collider) const;
    int witness_class(godot::Object *p_collider, bool p_declared_support) const;
    godot::Ref<NetwEntity> contact_entity(godot::Object *p_collider) const;
    godot::String collider_identity(godot::Object *p_collider) const;
    int classify_collider(
        godot::Object *p_collider,
        const godot::String &p_support
    ) const;
    int witness_class_of(
        godot::Object *p_collider,
        const godot::String &p_support
    ) const;
    godot::Dictionary open_simulated_state(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int64_t p_recv_tick,
        int64_t p_current_tick
    );

    enum RelayColumn {
        RELAY_DROPPED_LATE = 0,
        RELAY_RECORDED = 1,
        RELAY_COLUMN_COUNT = 2,
    };

    const SchemaRecord *input_schema(int64_t p_slot) const;
    godot::Array input_keys(int64_t p_slot) const;
    bool decode_command_frame(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload,
        predict::CommandFrameRecord &r_frame
    ) const;
    godot::PackedInt64Array admit_relayed_frame(
        int64_t p_slot,
        const predict::CommandFrameRecord &p_frame,
        const godot::Array &p_keys,
        bool p_reconcile_joint
    );

    godot::PackedByteArray build_command_frame_for(int64_t p_slot);
    godot::PackedInt64Array ack_frontier(int64_t p_slot, int64_t p_ack) const;

    int64_t lane_route(int64_t p_slot) const;
    void send_lane(
        int64_t p_peer,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_bytes
    );
    godot::Ref<NetwPredictStats> stats_of(int64_t p_slot) const;
    int64_t held_fact_of(int64_t p_slot, int p_fact) const;
    void set_held_fact(int64_t p_slot, int p_fact, int64_t p_value);
    void add_held_fact(int64_t p_slot, int p_fact, int64_t p_delta);
    godot::PackedByteArray publish_ack_frame(int64_t p_slot);
    void send_command_frame(int64_t p_slot);
    void send_ack_frame(int64_t p_slot);
    void admit_command_payload(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload
    );
    void admit_relayed_payload(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload
    );
    void declare_skipped_run(int64_t p_slot, int64_t p_from, int64_t p_until);
    int64_t queued_span(int64_t p_slot) const;
    void resync_input_if_stranded(
        int64_t p_slot,
        int64_t p_ceiling,
        int64_t p_buffer
    );
    void resync_tape_if_stranded(
        int64_t p_slot,
        int64_t p_depth,
        int64_t p_buffer,
        int64_t p_ceiling
    );
    void report_owner_claim(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_fingerprint
    );
    godot::Dictionary capture_state_raw(int64_t p_slot);
    godot::Dictionary capture_input_raw(int64_t p_slot);
    void apply_input_raw(int64_t p_slot, const godot::Dictionary &p_payload);
    godot::Dictionary canonical_state(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    );
    godot::Dictionary canonical_input(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    );
    godot::PackedByteArray input_bytes(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    );
    godot::Dictionary capture_canonical_state(int64_t p_slot);
    void record_input_bytes(
        int64_t p_slot,
        int64_t p_tick,
        const godot::Dictionary &p_input
    );
    void run_replay_step(
        int64_t p_slot,
        const godot::Dictionary &p_input,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );
    bool replay_tape_entry(int64_t p_slot, double p_delta, int64_t p_tick);
    godot::Dictionary command_for(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_label
    ) const;
    void prepare_tick_tape(int64_t p_slot, int64_t p_tick);
    void refresh_simulation_gate(int64_t p_slot, bool p_force_release);
    void host_local_step(int64_t p_slot, double p_delta, int64_t p_tick);
    int64_t sibling_slot(const godot::Ref<NetwEntity> &p_member);
    int admitted_reconcile_mode(int64_t p_slot);
    void admit_reconcile_mode(int64_t p_slot);
    void follow_relay_subscription(int64_t p_slot, int p_mode);
    bool slot_has_stepper(int64_t p_slot) const;
    bool slot_is_steppable(int64_t p_slot) const;
    void refresh_tape_diagnostics(int64_t p_slot);
    int64_t refresh_owner_ack_age(int64_t p_slot);
    bool speculation_horizon_full(int64_t p_slot);
    godot::PackedByteArray build_command_frame(
        int64_t p_slot,
        const SchemaRecord &p_schema,
        const godot::Array &p_keys,
        int p_window
    );

    godot::PackedInt64Array admit_command_frame(
        int64_t p_slot,
        const predict::CommandFrameRecord &p_frame,
        const godot::Array &p_keys
    );

    int64_t receive_command_frame(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload
    );
    godot::PackedInt64Array receive_relayed_command_frame(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload,
        bool p_reconcile_joint
    );

    int64_t drive_frontier(int64_t p_slot) const;
    int replay_reseed_horizon(
        int64_t p_slot,
        int64_t p_ack,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote
    );
    void finish_reseed_alignment(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_ack,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote
    );
    void run_input(
        int64_t p_slot,
        const godot::Dictionary &p_input,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );
    void consume_one(int64_t p_slot, double p_delta);
    int replay_tick_window(
        int64_t p_slot,
        int64_t p_from,
        int64_t p_to,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote
    );
    int replay_authored_entries(
        int64_t p_slot,
        int64_t p_ack,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote
    );
    godot::Dictionary close_replayed_entry(
        int64_t p_slot,
        int64_t p_index,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants
    );

    godot::Dictionary seal_transition(
        int64_t p_slot,
        int64_t p_transition,
        const godot::Dictionary &p_state,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants
    );

    void ledger_note_writes(int64_t p_slot, const godot::Dictionary &p_deltas);
    godot::Array state_columns_of(
        int64_t p_slot,
        const godot::Dictionary &p_payload
    ) const;
    godot::PackedFloat64Array tolerance_columns_of(
        int64_t p_slot,
        const godot::Dictionary &p_tolerances
    ) const;

    RecoveryPlan recovery_of(
        int64_t p_slot,
        const predict::WritePlan &p_plan
    ) const;
    godot::Dictionary restore_payload_of(
        int64_t p_slot,
        const predict::JointPassPlan &p_plan,
        int p_index
    ) const;

    bool apply_restore(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int p_operator,
        int64_t p_basis,
        int64_t p_provenance_slot,
        const godot::StringName &p_target,
        int p_ack_age_ticks,
        double p_divergence_epsilon,
        bool p_evidence_free,
        bool p_pool_planned
    );

    godot::Dictionary write_deltas(
        int64_t p_slot,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_staged
    );
    bool recent_witness_clean(int64_t p_slot, int64_t p_basis) const;
    godot::Dictionary try_transport(
        int64_t p_slot,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authority,
        const godot::Dictionary &p_current,
        int64_t p_basis,
        bool p_escalated,
        const godot::Callable &p_corridor,
        double p_teleport_default,
        double p_divergence_epsilon,
        int p_recovery_policy
    );

    godot::Dictionary solve_evidence(
        int64_t p_slot,
        const godot::Dictionary &p_base_facts,
        int64_t p_transition,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants
    );
    bool contact_breaches_boundary(
        int64_t p_slot,
        godot::Object *p_collider,
        const godot::String &p_support,
        const godot::PackedStringArray &p_participants
    ) const;

    int judge_carry(
        int64_t p_slot,
        const godot::StringName &p_field,
        bool p_same_type,
        bool p_finite,
        bool p_within_envelope,
        bool p_pure,
        bool p_faithful
    );
    int decline_carry(int64_t p_slot, const godot::StringName &p_field);

    bool carry_eligible(int64_t p_slot, const godot::StringName &p_field) const;
    bool carry_retired(int64_t p_slot, const godot::StringName &p_field) const;
    godot::PackedInt64Array carry_stats(
        int64_t p_slot,
        const godot::StringName &p_field
    ) const;

    void mark_carry_dirty(int64_t p_slot, int64_t p_transition);
    bool carry_judgeable(int64_t p_slot, int64_t p_transition) const;

    predict::CarryAttempt attempt_carry(
        int64_t p_slot,
        const godot::StringName &p_field,
        const godot::Variant &p_acknowledged,
        int64_t p_basis,
        double p_teleport_default,
        double p_divergence_epsilon
    );

    godot::LocalVector<predict::ReplayEntry> replay_entries(
        int64_t p_slot,
        int64_t p_basis
    ) const;
    godot::Dictionary state_before(int64_t p_slot, int64_t p_transition) const;

    void close_drive(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_post_fp,
        int64_t p_post_pose_fp,
        int64_t p_post_momentum_fp,
        int64_t p_post_controller_fp
    );

    void mark_domain(int64_t p_slot, int64_t p_transition, int p_domain);
    void mark_chain_broken(int64_t p_slot, int64_t p_transition);
    void mark_provenance(
        int64_t p_slot,
        int64_t p_transition,
        int p_episode_id,
        int p_write_id,
        int p_operator,
        int64_t p_basis
    );
    void mark_attribution(
        int64_t p_slot,
        int64_t p_transition,
        int p_attribution
    );
    void mark_differing_family(
        int64_t p_slot,
        int64_t p_transition,
        int p_family
    );
    void mark_solve(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_topo_fp,
        int64_t p_witness_fp,
        int p_evidence_mask,
        int p_witness_class_bits
    );
    bool witness_judged(int64_t p_slot, int64_t p_transition) const;
    void mark_witness_match(
        int64_t p_slot,
        int64_t p_transition,
        bool p_matched
    );
    void charge_divergence(
        int64_t p_slot,
        const predict::AckFrame &p_frame,
        int64_t p_transition,
        int p_index,
        bool p_complete,
        int p_attribution
    );
    void declare_skipped(int64_t p_slot, int64_t p_transition, int64_t p_label);

    void mark_aligned_error(
        int64_t p_slot,
        int64_t p_transition,
        double p_error
    );

    void acknowledge(int64_t p_slot, int64_t p_transition, bool p_matched);

    predict::AckVerdict admit_ack(
        int64_t p_slot,
        int64_t p_transition,
        const predict::EvidenceRow &p_evidence,
        bool p_substituted
    );

    enum AckRunColumn {
        ACK_RUN_OF_ACKS = 0,
        ACK_RUN_SUBSTITUTED = 1,
        ACK_RUN_VERIFIED = 2,
        ACK_RUN_MISMATCHED = 3,
        ACK_RUN_FIRST_DIVERGENT = 4,
        ACK_RUN_AGE = 5,
        ACK_RUN_RECEIPT = 6,
        ACK_RUN_COLUMN_COUNT = 7,
    };

    enum AckReceipt {
        ACK_RECEIPT_UNDECODED = 0,
        ACK_RECEIPT_FOREIGN_EPOCH = 1,
        ACK_RECEIPT_ADMITTED = 2,
    };

    godot::PackedInt64Array admit_ack_run(
        int64_t p_slot,
        const predict::AckFrame &p_frame,
        const godot::Callable &p_quarantine_witness,
        const godot::Callable &p_retry_operator
    );

    godot::PackedInt64Array receive_ack_frame(
        int64_t p_slot,
        const godot::PackedByteArray &p_payload,
        int64_t p_tape_epoch,
        bool p_confirm_reseed,
        const godot::Callable &p_quarantine_witness,
        const godot::Callable &p_retry_operator
    );

    predict::AckVerdict admit_ack_row(
        int64_t p_slot,
        const predict::AckFrame &p_frame,
        int64_t p_transition,
        int p_index,
        bool p_complete,
        bool p_substituted
    );

    predict::StateVerdict compare_state_for(
        int64_t p_slot,
        int64_t p_transition,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authority,
        const godot::PackedFloat64Array &p_meter_tolerances,
        double p_divergence_epsilon
    );
    predict::StateVerdict compare_state(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_transition,
        const godot::Array &p_predicted,
        const godot::Array &p_authority,
        const godot::PackedFloat64Array &p_correction_tolerances,
        const godot::PackedFloat64Array &p_meter_tolerances,
        double p_fallback_epsilon,
        bool p_stream_reconstructed,
        bool p_ack_domain_confirmed
    );

    void set_state(int64_t p_slot, const godot::Array &p_state);
    bool state_has(int64_t p_slot, int p_field) const;
    godot::Variant state_at(int64_t p_slot, int p_field) const;
    predict::WritePlan recover(
        int64_t p_slot,
        const predict::RecoveryRequest &p_request
    );
    predict::WritePlan recover_for(
        int64_t p_slot,
        int64_t p_basis,
        int64_t p_current_label,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authority,
        const godot::Dictionary &p_current,
        const godot::Dictionary &p_tier_errors,
        int p_policy,
        double p_divergence_epsilon,
        double p_teleport_threshold,
        int p_max_restore_ticks,
        int p_ack_age_ticks,
        int p_collision_cooldown_ticks,
        int p_domain,
        int p_attribution,
        bool p_suppressed
    );
    void open_recovery_window(int64_t p_slot, int64_t p_label, int p_cooldown);
    void suppress_recovery_until(
        int64_t p_slot,
        int64_t p_label,
        int p_cooldown
    );
    bool escalation_pending(int64_t p_slot) const;

    enum RecoveryWindow {
        RECOVERY_DISSIPATED = 0,
        RECOVERY_TRANSPORTED = 1,
        RECOVERY_PLAN = 2,
    };

    godot::Dictionary capture_current(int64_t p_slot);
    godot::Dictionary recovery_before_of(int64_t p_slot) const;
    godot::Dictionary recovery_write_of(int64_t p_slot) const;
    godot::Dictionary recovery_projection_of(int64_t p_slot) const;
    godot::Dictionary recovery_carried_of(int64_t p_slot) const;
    godot::Dictionary recovery_tier_errors_of(int64_t p_slot) const;
    predict::WritePlan plan_recovery(
        int64_t p_slot,
        int64_t p_ack,
        int64_t p_ack_label,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        bool p_escalated,
        int p_domain,
        int p_attribution
    );
    void apply_recovery_plan(
        int64_t p_slot,
        const RecoveryPlan &p_plan,
        int64_t p_ack,
        bool p_pool_planned
    );
    int open_recovery(
        int64_t p_slot,
        int64_t p_ack,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        int p_meter,
        int p_domain
    );
    int64_t recovery_window_until(int64_t p_slot) const;
    int64_t recovery_cooldown_until(int64_t p_slot) const;
    EpisodeReport episode(int64_t p_slot) const;
    void enter_quarantine(
        int64_t p_slot,
        int64_t p_transition,
        bool p_stream_reconstructed,
        int p_attribution,
        bool p_demoted
    );
    predict::WritePlan quarantine_state(
        int64_t p_slot,
        int64_t p_tick,
        int64_t p_basis,
        const godot::Array &p_payload,
        bool p_whole
    );
    predict::WritePlan quarantine_witness(
        int64_t p_slot,
        int64_t p_basis,
        int p_bits
    );
    void confirm_reseed_epoch(int64_t p_slot);
    predict::WritePlan align_reseed(
        int64_t p_slot,
        int64_t p_transition,
        const godot::Array &p_payload,
        int64_t p_ignore_through
    );
    bool admit_post_reseed(int64_t p_slot, int64_t p_basis);
    void adopt_alignment(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_ignore_through
    );
    bool finish_probation(int64_t p_slot, bool p_corrected);
    bool quarantine_latched(int64_t p_slot) const;
    int quarantine_clean_run(int64_t p_slot) const;
    int quarantine_target(int64_t p_slot) const;
    bool reseed_align_pending(int64_t p_slot) const;
    bool reseed_epoch_confirmed(int64_t p_slot) const;
    bool probation_pending(int64_t p_slot) const;
    int64_t reseed_ignore_through(int64_t p_slot) const;

    godot::TypedArray<NetwEntity> live_participants(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island
    );
    godot::PackedStringArray live_participant_ids(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island
    );
    void publish_topology_roster(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island
    );
    void refresh_island_membership(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island,
        bool p_apply_promotion,
        const godot::Callable &p_admit_reconcile
    );
    void publish_island_roster(int64_t p_slot);
    void clear_island_promotions(int64_t p_slot);
    bool apply_island_promotions(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island,
        const godot::TypedArray<NetwEntity> &p_promoted
    );
    int release_lingering(int64_t p_slot, int64_t p_floor_transition);
    bool contact_is_equivalent(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island,
        bool p_has_witness
    );
    void notify_contact(
        int64_t p_slot,
        const godot::Ref<NetwPredictIsland> &p_island,
        bool p_has_witness,
        int p_cooldown_ticks
    );
    godot::TypedArray<NetwEntity> island_roster(
        int64_t p_slot,
        godot::Object *p_session,
        const godot::Ref<NetwPredictIsland> &p_island
    );
    godot::TypedArray<NetwEntity> island_commit_members(
        int64_t p_slot,
        const godot::TypedArray<NetwEntity> &p_members,
        const godot::Ref<NetwPredictIsland> &p_island,
        int64_t p_frontier
    );
    godot::PackedInt64Array island_commit(
        int64_t p_slot,
        int64_t p_owner_order_key,
        const godot::PackedInt64Array &p_members,
        const godot::PackedInt64Array &p_order_keys,
        const godot::PackedFloat64Array &p_distance_squared,
        const godot::PackedInt32Array &p_fidelities,
        const godot::PackedByteArray &p_eligible,
        const godot::PackedByteArray &p_contact,
        int p_promotion,
        int p_promotion_count,
        double p_promotion_meters,
        int64_t p_frontier
    );
    int island_member_count(int64_t p_slot) const;
    bool island_promoted(int64_t p_slot, int64_t p_member) const;
    double island_distance_squared(int64_t p_slot, int64_t p_member) const;
    int64_t tenure_begin(int64_t p_slot) const;
    int64_t tenure_end(int64_t p_slot) const;
    void joint_record(
        int64_t p_slot,
        int64_t p_transition,
        const godot::Array &p_state,
        const godot::Variant &p_command,
        bool p_authored,
        bool p_relayed,
        bool p_predictor_valid
    );
    void joint_note_basis(int64_t p_slot, int64_t p_basis, int p_source);
    void note_joint_basis(
        int64_t p_slot,
        int64_t p_basis,
        const godot::Dictionary &p_payload,
        int p_source
    );
    void admit_simulated_state(
        int64_t p_slot,
        const godot::Dictionary &p_header
    );
    void joint_clear(int64_t p_slot);
    godot::Variant joint_command_at(int64_t p_slot, int64_t p_transition) const;
    int joint_provenance_at(int64_t p_slot, int64_t p_transition) const;
    predict::JointPassPlan joint_pass(int64_t p_slot, int64_t p_present);
    godot::PackedInt64Array joint_stats(int64_t p_slot) const;

    enum IslandMode {
        ISLAND_NONE = 0,
        ISLAND_DECLARED = 1,
        ISLAND_JOINT = 2,
    };

    enum CarryVerdictBits {
        CARRY_CARRIED = 0,
        CARRY_DECLINED = 1,
        CARRY_UNFAITHFUL = 2,
        CARRY_RETIRED_ALREADY = 3,
        CARRY_RETIRED_SCHEDULE = 4,
        CARRY_RETIRED_IMPURE = 5,
        CARRY_RETIRED_INFIDELITY = 6,
    };

    enum JointFloorSource {
        JOINT_FLOOR_ACK = 0,
        JOINT_FLOOR_STATE = 1,
        JOINT_FLOOR_RELAY = 2,
        JOINT_FLOOR_EPOCH = 3,
    };

    enum JointStat {
        STAT_JOINT_PASSES = 0,
        STAT_JOINT_MEMBERS = 1,
        STAT_JOINT_CELLS_RELAYED = 2,
        STAT_JOINT_CELLS_SUBSTITUTED = 3,
        STAT_JOINT_HEAL_SNAPS = 4,
        STAT_JOINT_LINGER_HELD = 5,
        STAT_JOINT_FLOOR = 6,
        STAT_JOINT_PRESENT = 7,
        STAT_JOINT_MAX_DEPTH = 8,
        STAT_JOINT_FLOOR_ACK_MOVES = 9,
        STAT_JOINT_FLOOR_STATE_MOVES = 10,
        STAT_JOINT_FLOOR_RELAY_MOVES = 11,
        STAT_JOINT_FLOOR_EPOCH_MOVES = 12,
        STAT_JOINT_COUNT = 13,
    };

    int journal_size(int64_t p_slot) const;
    void record_witness_detail(
        int64_t p_slot,
        int64_t p_transition,
        const godot::Dictionary &p_detail
    );
    void clear_witness_details(int64_t p_slot);

    void hold_deferred_operator(
        int64_t p_slot,
        int64_t p_basis,
        int64_t p_recv_tick,
        const godot::Dictionary &p_payload
    );
    enum DeferVerdict {
        DEFER_REFUSED = 0,
        DEFER_HELD = 1,
        DEFER_DISPLACED = 2,
    };

    int defer_operator_for_witness(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_basis,
        const godot::Dictionary &p_payload,
        bool p_has_corridor,
        bool p_observing
    );
    int64_t deferred_operator_basis(int64_t p_slot) const;
    int64_t deferred_operator_recv_tick(int64_t p_slot) const;
    godot::Dictionary release_deferred_operator(int64_t p_slot);
    void drop_deferred_operator(int64_t p_slot);

    void set_island_participants(
        int64_t p_slot,
        const godot::PackedStringArray &p_names
    );
    godot::PackedStringArray island_participants(int64_t p_slot) const;
    godot::Dictionary topology_facts(
        int64_t p_slot,
        int p_schedule,
        int64_t p_epoch
    ) const;

    enum StampColumn {
        STAMP_BOUND = 0,
        STAMP_PRE_FP = 1,
        STAMP_POSE_FP = 2,
        STAMP_MOMENTUM_FP = 3,
        STAMP_CONTROLLER_FP = 4,
        STAMP_RAW_FP = 5,
        STAMP_EVIDENCE_MASK = 6,
        STAMP_COLUMN_COUNT = 7,
    };

    godot::PackedInt64Array open_state_stamp(
        int64_t p_slot,
        bool p_raw_enabled
    );

    godot::Dictionary transport_deltas(
        int64_t p_slot,
        const godot::Dictionary &p_current,
        const godot::Dictionary &p_restore
    ) const;
    static bool transport_moved(const godot::Dictionary &p_deltas);

    godot::PackedFloat64Array correction_tolerances(
        int64_t p_slot,
        double p_fallback_epsilon
    ) const;
    godot::PackedFloat64Array meter_tolerances(
        int64_t p_slot,
        int p_domain,
        double p_fallback_epsilon
    ) const;

    int meter_of(
        int64_t p_slot,
        const godot::Dictionary &p_field_divergence,
        int p_domain,
        double p_fallback_epsilon
    ) const;

    godot::Dictionary advanced_seed(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int64_t p_basis,
        int p_snap_restore,
        int p_max_restore_ticks,
        double p_teleport_threshold,
        double p_divergence_epsilon
    );

    godot::Dictionary project_state(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        double p_age
    ) const;
    bool has_pose_fields(int64_t p_slot) const;
    godot::Dictionary pose_errors(
        int64_t p_slot,
        const godot::Dictionary &p_current,
        const godot::Dictionary &p_target
    ) const;

    void validate_declaration(
        int64_t p_slot,
        const godot::Ref<NetwPropertySet> &p_set,
        const godot::Callable &p_emit_findings
    );
    godot::Dictionary reachability_report_of(int64_t p_slot) const;

    godot::Dictionary reachability_report(
        int64_t p_slot,
        const godot::StringName &p_entity_id,
        double p_divergence_epsilon,
        double p_teleport_threshold,
        int p_breach_response,
        const godot::StringName &p_breach_source,
        int p_island_claim,
        int p_contact_window_ticks,
        bool p_has_corridor
    ) const;

    godot::Dictionary dissipate_fields(
        int64_t p_slot,
        const godot::Dictionary &p_field_divergence,
        int p_domain,
        double p_fallback_epsilon
    ) const;

    godot::Dictionary non_pose_eligibility(
        int64_t p_slot,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authority,
        double p_fallback_epsilon
    ) const;
    godot::Dictionary episode_record(int64_t p_slot) const;
    godot::Dictionary episode_digest(int64_t p_slot) const;
    int64_t episode_revision(int64_t p_slot) const;
    void stamp_episode_revision(int64_t p_slot);
    void sync_episode(int64_t p_slot);
    void close_episode(int64_t p_slot);
    void record_episode_comparison(int64_t p_slot, int64_t p_previous_state);

    enum SettleOutcome {
        SETTLE_PROCEED = 0,
        SETTLE_REQUARANTINED = 1,
        SETTLE_REFUSED = 2,
    };

    int settle_comparison(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_ack,
        bool p_reconstructed,
        bool p_corrected,
        bool p_settled,
        double p_divergence,
        bool p_probation_before,
        int64_t p_episode_state_before,
        const godot::Callable &p_enter_fallback
    );

    godot::Ref<NetwPredictJournal> journal_snapshot(int64_t p_slot) const;

    void bind_session(NetwMultiplayer *p_core);

    void report_predict(
        int64_t p_slot,
        int64_t p_event,
        const godot::Dictionary &p_detail,
        const godot::Dictionary &p_model
    );
    void report_consume(
        int64_t p_slot,
        int64_t p_depth,
        int64_t p_buffer,
        int64_t p_action
    );
    void announce_episode(
        int64_t p_slot,
        int64_t p_event,
        const godot::Dictionary &p_extra
    );
    bool announce_recovered(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_attribution,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_correction_write
    );
    void announce_divergence(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_attribution,
        double p_divergence
    );
    void report_authority_model(int64_t p_slot);
    int64_t property_class_report_hash(
        int64_t p_slot,
        const godot::Ref<NetwPropertySet> &p_set
    ) const;

    godot::Dictionary field_divergence(
        int64_t p_slot,
        const predict::StateVerdict &p_verdict
    ) const;

    int transition_span(int64_t p_slot, int64_t p_basis) const;

    int64_t journal_epoch(int64_t p_slot) const;
    godot::PackedInt64Array journal_transitions(int64_t p_slot) const;
    predict::JournalRow journal_row(int64_t p_slot, int64_t p_transition) const;

    int journal_slot_of(int64_t p_slot, int64_t p_transition) const;

    int64_t journal_transition_at(int64_t p_slot, int p_index) const;
    int64_t journal_label_at(int64_t p_slot, int p_index) const;
    int journal_kind_at(int64_t p_slot, int p_index) const;
    int64_t journal_c_hash_at(int64_t p_slot, int p_index) const;
    int64_t journal_pre_fp_at(int64_t p_slot, int p_index) const;
    int64_t journal_post_fp_at(int64_t p_slot, int p_index) const;
    int64_t journal_environment_at(int64_t p_slot, int p_index) const;
    int64_t journal_topology_at(int64_t p_slot, int p_index) const;
    int64_t journal_raw_at(int64_t p_slot, int p_index) const;
    int64_t journal_witness_at(int64_t p_slot, int p_index) const;
    int journal_witness_class_at(int64_t p_slot, int p_index) const;
    godot::PackedInt32Array journal_pre_families_at(
        int64_t p_slot,
        int p_index
    ) const;
    godot::PackedInt32Array journal_post_families_at(
        int64_t p_slot,
        int p_index
    ) const;
    int journal_episode_id_at(int64_t p_slot, int p_index) const;
    int journal_write_id_at(int64_t p_slot, int p_index) const;
    int journal_operator_at(int64_t p_slot, int p_index) const;
    int64_t journal_basis_at(int64_t p_slot, int p_index) const;
    int journal_evidence_at(int64_t p_slot, int p_index) const;
    int journal_flags_at(int64_t p_slot, int p_index) const;
    int journal_domain_at(int64_t p_slot, int p_index) const;
    int journal_attribution_at(int64_t p_slot, int p_index) const;
    int journal_differing_family_at(int64_t p_slot, int p_index) const;
    double journal_aligned_error_at(int64_t p_slot, int p_index) const;
    int64_t journal_last_closed(int64_t p_slot) const;
    int64_t journal_first_unmatched(int64_t p_slot) const;
    int64_t journal_first_chain_break(int64_t p_slot) const;
    void journal_clear(int64_t p_slot, int64_t p_epoch);
    void tape_reset(int64_t p_slot, int64_t p_epoch);
    godot::PackedByteArray build_ack_frame(
        int64_t p_slot,
        int p_epoch,
        int64_t p_ack,
        int64_t p_owner_ack_floor
    ) const;

    int tape_size(int64_t p_slot) const;
    godot::PackedInt64Array tape_span(int64_t p_slot) const;
    int64_t tape_label_of(int64_t p_slot, int64_t p_index) const;
    bool tape_is_fresh(int64_t p_slot, int64_t p_index) const;
    void tape_prepare_tick(int64_t p_slot, int64_t p_tick);
    void tape_author(int64_t p_slot, int64_t p_label, bool p_fresh);

    void bind_timeline(
        int64_t p_slot,
        const godot::Ref<NetwTimeline> &p_timeline
    );
    bool wire_role_timeline(
        int64_t p_slot,
        int p_role,
        bool p_fallback_latched,
        const godot::Ref<NetwTimeline> &p_declared
    );
    godot::Ref<NetwTimeline> entry_history(int64_t p_slot) const;
    godot::Ref<NetwTimeline> timeline_of(int64_t p_slot) const;
    void declare_axes(
        int64_t p_slot,
        bool p_authority,
        bool p_controlled_locally,
        bool p_inputless
    );
    void set_declared_epoch(int64_t p_slot, int64_t p_epoch);
    int64_t declared_epoch_of(int64_t p_slot) const;

    godot::Dictionary record_drive(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_label,
        int p_kind,
        const godot::Dictionary &p_input,
        int64_t p_drive_tick,
        bool p_caller_selected,
        bool p_input_recorded = false,
        bool p_authoring = false
    );

    godot::Dictionary select_comparison(int64_t p_slot, int64_t p_ack);

    enum Admission {
        ADMIT_CLOSED = 0,
        ADMIT_PROCEED = 1,
        ADMIT_REALIGN = 2,
        ADMIT_REALIGN_PENDING = 3,
        ADMIT_RESEED_IGNORED = 4,
    };

    int admit_state(int64_t p_slot, int64_t p_ack);

    enum ComparisonColumn {
        COMPARE_DOMAIN = 0,
        COMPARE_ROW_FLAGS = 1,
        COMPARE_EPISODE_STATE = 2,
        COMPARE_PROBATION = 3,
        COMPARE_RECONSTRUCTED = 4,
        COMPARE_COLUMN_COUNT = 5,
    };

    godot::PackedInt64Array open_comparison(int64_t p_slot, int64_t p_ack);

    godot::Dictionary open_state_comparison(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_ack,
        const godot::Dictionary &p_payload,
        const godot::Callable &p_realign
    );

    int refuse_recovery(int64_t p_slot, bool p_corrected);
    int attribution_for(int64_t p_slot, int64_t p_transition) const;
    godot::Dictionary pose_errors_against(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int p_ack_age_ticks
    );

    int trigger_shape(int64_t p_slot, double p_fallback_epsilon) const;
    void record_restore(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int p_operator,
        int64_t p_basis,
        int64_t p_provenance_slot,
        const godot::StringName &p_target,
        int p_ack_age_ticks,
        double p_divergence_epsilon,
        bool p_evidence_free,
        bool p_pool_planned
    );

    godot::Dictionary carry_payload(
        int64_t p_slot,
        const godot::Dictionary &p_payload,
        int64_t p_basis,
        double p_teleport_default,
        double p_divergence_epsilon
    );
    godot::LocalVector<predict::CarryAttempt> carry_attempts(
        int64_t p_slot
    ) const;
    void report_carry_retirements(int64_t p_slot);

    void reset_for_rewire(
        int64_t p_slot,
        bool p_was_wired,
        bool p_same_stream,
        bool p_raw_fingerprints,
        const godot::Dictionary &p_stall_input
    );

    bool declared_authority_of(int64_t p_slot) const;
    bool declared_controlled_locally_of(int64_t p_slot) const;

    void bind_property_sets(
        int64_t p_slot,
        const godot::Ref<NetwPropertySetBinding> &p_state,
        const godot::Ref<NetwPropertySetBinding> &p_input
    );
    godot::Ref<NetwPropertySetBinding> state_binding_of(int64_t p_slot) const;
    godot::Ref<NetwPropertySetBinding> input_binding_of(int64_t p_slot) const;
    void trim_history(int64_t p_slot, int64_t p_ack);

    bool command_admit(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_label,
        bool p_fresh,
        const godot::Dictionary &p_command
    );
    bool command_has(int64_t p_slot, int64_t p_transition) const;
    int64_t command_label_of(int64_t p_slot, int64_t p_transition) const;
    bool command_is_fresh(int64_t p_slot, int64_t p_transition) const;
    godot::Dictionary command_payload_of(
        int64_t p_slot,
        int64_t p_transition
    ) const;
    int command_depth_from(int64_t p_slot, int64_t p_cursor) const;
    godot::PackedInt64Array command_transitions(int64_t p_slot) const;

    enum DriveStat {
        STAT_DRIVE_SEQ = 0,
        STAT_LAST_DRIVE_LABEL = 1,
        STAT_LAST_DRIVE_KIND = 2,
        STAT_TAPE_EPOCH = 3,
        STAT_TAPE_INDEX = 4,
        STAT_ACK_AGE_TICKS = 5,
        STAT_AUTHORING_CLAMPED = 6,
        STAT_SPECULATION_HELD = 7,
        STAT_CHAIN_BREAKS = 8,
        STAT_QUANTUM_STEPS = 9,
        STAT_QUANTUM_DECLARED = 10,
        STAT_QUANTUM_FAULTS = 11,
        STAT_OWNER_LOST = 12,
        STAT_MAX_REPLAY_DEPTH = 13,
        STAT_COUNT = 14,
    };

    bool run_recovery_ladder(
        int64_t p_slot,
        int64_t p_ack,
        int64_t p_ack_label,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        int p_meter,
        int p_domain,
        int p_attribution,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote,
        const godot::Callable &p_seam
    );
    RecoveryPlan recover_through(
        int64_t p_slot,
        const godot::Dictionary &p_context,
        const godot::Dictionary &p_before,
        const godot::Callable &p_seam
    );
    bool plan_answered(
        const godot::Ref<NetwPredictRecovery> &p_answered,
        int64_t p_slot,
        RecoveryPlan &r_plan
    );
    bool judgement_answered(
        const godot::Ref<NetwPredictJudgement> &p_answered,
        int64_t p_slot,
        Judgement &r_judged
    );
    void refuse_seam(const godot::StringName &p_seam, int64_t p_slot);
    godot::Dictionary judge_state(
        int64_t p_slot,
        int64_t p_ack,
        int64_t p_domain,
        int64_t p_row_flags,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Callable &p_seam
    );
    void close_state_recovery(
        int64_t p_slot,
        int64_t p_ack,
        const godot::Dictionary &p_payload,
        const RecoveryPlan &p_plan,
        int64_t p_attribution,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_correction_write,
        const godot::Dictionary &p_sample,
        const godot::PackedStringArray &p_participants,
        const godot::Callable &p_demote
    );
    void note_replay_depth(int64_t p_slot, int p_depth);
    void record_idle_drive(int64_t p_slot, int64_t p_label, int p_kind);
    void record_authoring_clamp(int64_t p_slot);
    void record_speculation_hold(int64_t p_slot);
    void refresh_ack_age(int64_t p_slot);
    int mark_authority_ack(int64_t p_slot, int64_t p_transition);
    enum DriveCursor {
        CURSOR_TAPE_EPOCH = 0,
        CURSOR_NEXT_TAPE_ENTRY = 1,
        CURSOR_LAST_DRIVEN_ENTRY = 2,
        CURSOR_LATEST_INPUT_TICK = 3,
        CURSOR_LAST_DRIVEN_INPUT_TICK = 4,
        CURSOR_LAST_FRAME_TRANSITION_TICK = 5,
        CURSOR_COUNT = 6,
    };

    bool journal_has(int64_t p_slot, int64_t p_transition) const;
    godot::PackedInt64Array drive_cursors(int64_t p_slot) const;
    godot::PackedInt64Array drive_stats(int64_t p_slot) const;

    enum CompareStat {
        STAT_COMPARISONS_RAN = 0,
        STAT_COMPARISONS_SKIPPED = 1,
        STAT_FP_VERIFIED = 2,
        STAT_FP_MISMATCHES = 3,
        STAT_FIRST_DIVERGENT_TRANSITION = 4,
        STAT_COMPARE_COUNT = 5,
    };

    godot::PackedInt64Array compare_stats(int64_t p_slot) const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredictionEngine::Admission);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::SettleOutcome);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::RecoveryWindow);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::ComparisonColumn);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::RelayColumn);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::AckReceipt);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::Roster);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DriveStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DriveCursor);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::CompareStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::IslandMode);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::PassPhase);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::CarryVerdictBits);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DissipateDecision);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DeferVerdict);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::AckRunColumn);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::EpisodeStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::ClaimColumn);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::StampColumn);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::JointFloorSource);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::JointStat);
