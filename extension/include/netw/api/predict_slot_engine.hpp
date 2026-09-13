#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/timeline.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/slot_verdicts.hpp"

namespace netw {

class NetwMultiplayer;
class NetwPredictionHandle;
class NetwReparentOpts;

class NetwPredictSlotEngine {
    friend class NetwMultiplayer;
    friend class NetwPredictionHandle;

    struct Declaration {
        godot::Ref<NetwPropertySetBinding> state;
        godot::Ref<NetwPropertySetBinding> input;
        godot::Ref<NetwTimeline> timeline;
        bool authority = false;
        bool controlled_locally = false;
    };

    godot::ObjectID core_id;
    NetwPredictionEngine *pool = nullptr;
    godot::Ref<NetwEntity> entity;
    godot::Ref<NetwPredictionHandle> handle;
    Declaration declaration;

    NetwMultiplayer *core_seated() const;
    NetwPredictSlotEngine *sibling(
        const godot::Ref<NetwEntity> &p_member
    ) const;
    bool overrides_seam(const godot::StringName &p_seam) const;
    Declaration resolved_declaration() const;
    Declaration declaration_of(const godot::Ref<NetwEntity> &p_entity) const;

    int role() const;
    void set_role_column(int p_role);
    int correction() const;
    void set_correction_column(int p_correction);
    godot::Ref<NetwPropertySetBinding> state_binding() const;
    godot::Ref<NetwPropertySetBinding> input_binding() const;
    godot::Ref<NetwTimeline> timeline() const;
    godot::Ref<NetwTimeline> entry_history() const;
    double tick_delta() const;
    bool fallback_latched() const;
    int64_t schedule() const;
    int64_t ack() const;
    bool ack_advanced() const;

    int64_t route() const;
    predict::SlotCursors cursors() const;

    void apply_scene_island_defaults();
    void rewire_on(const Declaration &p_declaration);
    void refresh_simulation_gate(bool p_force_release);
    void publish_topology_roster();
    void adopt_timing(const NetwPredictTiming &p_timing);
    void charge_speculation_hold();
    void charge_authoring_clamp();
    void sync_episode();
    bool defer_operator_for_witness(
        int64_t p_recv_tick,
        int64_t p_basis,
        const godot::Dictionary &p_payload
    );
    void begin_reseed(const predict::WritePlan &p_plan);
    void send_command_frame();
    void send_ack_frame();
    void file_command_cell(
        int64_t p_transition,
        const godot::Dictionary &p_command,
        int p_origin
    );
    godot::Dictionary command_cell_at(int64_t p_transition) const;
    void judge_owner_claim(int64_t p_transition, int64_t p_fingerprint);
    int64_t recorded_transition() const;
    void record_input_to_pool(int64_t p_tick, const godot::Dictionary &p_input);
    int resolve_correction(int p_declared) const;
    int resolve_axes();
    int pool_island() const;
    void admit_reconcile_mode();
    void clear_island_promotions();

    void predict_author_tick(int64_t p_tick);
    void predict_frame_step(const NetwPredictTiming &p_timing);
    void simulate_solver(const NetwPredictTiming &p_timing);
    void finalize_solver_state();
    Fold drive_fold(int64_t p_frame_tick);
    int consume_action(int p_depth, int p_buffer);
    void fallback_author_tick(int64_t p_tick);
    void fallback_author_frame_step(const NetwPredictTiming &p_timing);
    void fallback_author_step(int64_t p_tick);
    void predict_step(double p_delta, int64_t p_tick);
    void simulated_step(double p_delta, int64_t p_tick);
    void simulated_frame_step(const NetwPredictTiming &p_timing);
    godot::Dictionary predicted_command(int64_t p_tick);
    void prepare_tick_tape(int64_t p_tick);
    bool speculation_horizon_full();
    void publish_ack_age(int64_t p_age);
    void on_state(
        int64_t p_recv_tick,
        int64_t p_ack,
        const godot::Dictionary &p_payload
    );

    godot::Dictionary restore_payload(
        const predict::JointPassPlan &p_plan,
        int p_index
    ) const;
    RecoveryPlan plan_of(const predict::WritePlan &p_plan) const;
    godot::Array state_columns(const godot::Dictionary &p_payload) const;
    void close_replayed_entry(int64_t p_index);
    bool is_steppable() const;
    godot::LocalVector<NetwPredictSlotEngine *> joint_group();
    void append_joint_member(
        godot::LocalVector<NetwPredictSlotEngine *> &r_out,
        const godot::Ref<NetwEntity> &p_member
    );
    struct SteppedIsland {
        godot::RID space;
        godot::Ref<NetwPhysicsStepper> stepper;
    };
    SteppedIsland stepped_island() const;
    void step_island(const SteppedIsland &p_island, int64_t p_transition);
    void run_joint_pass(
        const predict::JointPassPlan &p_plan,
        const godot::LocalVector<NetwPredictSlotEngine *> &p_members,
        int64_t p_floor_transition
    );
    void release_lingering(int64_t p_floor_transition);
    void joint_heal();
    void note_joint_writes(const godot::Dictionary &p_before);
    int64_t ledger_drive_frontier() const;
    predict::JointPassPlan pool_joint_pass(int64_t p_present);

    void host_local_author_tick(int64_t p_tick);
    void host_local_frame_step(const NetwPredictTiming &p_timing);
    void host_local_step(double p_delta, int64_t p_tick);

    void consume_step(double p_delta, int64_t p_server_tick);
    void consume_frame_step(const NetwPredictTiming &p_timing);
    void record_consume_cadence(int p_depth);
    void bump_consume_shape(int64_t p_consumed);
    void replay_tape_entry(double p_delta, int64_t p_timing_tick);
    void resync_tape_if_stranded(int p_depth, int p_buffer);
    int replay_depth() const;
    void resync_if_stranded();
    int64_t queued_span() const;
    void consume_one(double p_delta);

    godot::Dictionary record_drive(
        int64_t p_transition,
        int64_t p_label,
        int p_kind,
        const godot::Dictionary &p_input,
        int64_t p_drive_tick,
        bool p_caller_selected,
        bool p_input_recorded = false,
        bool p_authoring = false
    );
    void mark_idle_drive(int64_t p_label, int p_kind);
    void adopt_tape_position(int64_t p_transition);
    void author_command_entry(int64_t p_label, bool p_fresh);
    void refresh_tape_diagnostics();
    void run(
        const godot::Dictionary &p_input,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );
    godot::Dictionary capture() const;
    godot::Dictionary capture_input_raw() const;
    void apply_input_raw(const godot::Dictionary &p_payload);
    int64_t state_fingerprint(const godot::Dictionary &p_payload) const;
    void close_journal_row(
        int64_t p_transition,
        const godot::Dictionary &p_state
    );
    godot::Dictionary witness_sample() const;
    godot::PackedStringArray island_participant_ids() const;
    void restore(
        const godot::Dictionary &p_payload,
        int p_operator,
        int64_t p_basis,
        NetwPredictSlotEngine *p_provenance_owner = nullptr,
        bool p_evidence_free = false,
        bool p_pool_planned = false
    );
    void register_with_loop();
    void unregister_from_loop();

    void on_control_changed(int64_t p_previous_peer, int64_t p_peer);
    void on_reparented();
    void on_state_frame(const godot::Dictionary &p_header);
    void on_input_frame(const godot::Dictionary &p_header);
    void on_simulated_state_frame(const godot::Dictionary &p_header);
    void on_quarantine_state_frame(const godot::Dictionary &p_header);
    void apply_quarantine_witness(int64_t p_basis);
    void retry_deferred_operator(int64_t p_basis);
    void enter_fallback_at(int64_t p_transition, int p_attribution);
    void enter_fallback(
        int64_t p_transition,
        int p_attribution,
        bool p_demoted
    );
    void finish_reseed_alignment(
        int64_t p_recv_tick,
        int64_t p_ack,
        const godot::Dictionary &p_payload
    );
    void emit_reachability_findings(const godot::Array &p_findings);
    void maybe_demote_for_breach(
        int64_t p_transition,
        const godot::Dictionary &p_solve
    );
    godot::Ref<NetwPredictRecovery> recover_through_seam(
        const godot::Dictionary &p_carried,
        NetwPredict::RecoveryPolicy p_policy,
        NetwPredict::CorrectionMode p_correction,
        NetwPredict::RestoreMode p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_tier_errors,
        const godot::Dictionary &p_context,
        double p_tick_delta
    );
    godot::Ref<NetwPredictJudgement> evaluate_through_seam(
        NetwPredictJournal::Domain p_domain,
        NetwPredict::ExactVerdict p_exact_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_field_sink
    );

public:
    void attach(godot::Object *p_iface, const godot::Ref<NetwEntity> &p_entity);
    void release();

    void receive_command_frame(const godot::PackedByteArray &p_payload);
    void receive_relayed_command_frame(const godot::PackedByteArray &p_payload);
    void receive_ack_frame(const godot::PackedByteArray &p_payload);
    void notify_contact();

    int64_t native_slot() const;
    NetwPredictionEngine *seated_pool() const;
    godot::Ref<NetwEntity> seated_entity() const;
    NetwMultiplayer *core() const;

    void rewire();
    void reconfigure();
    void push_simulate();
    void follow_relay_subscription(int p_mode);

    int64_t order_key() const;
    void prepare_island(int p_schedule);
    void joint_pass(const NetwPredictTiming &p_timing);
    void network_tick(const NetwPredictTiming &p_timing);
    void simulate_tick(const NetwPredictTiming &p_timing);
    void simulate_frame(const NetwPredictTiming &p_timing);
    void simulate_stepped(const NetwPredictTiming &p_timing);
    void finalize_frame_state();
    void finalize_stepped_state();

    bool uses_schedule(int p_schedule) const;
    godot::Dictionary transition_state_at(int64_t p_entry_index) const;
    int64_t history_record_tick(int64_t p_fallback_tick) const;
    bool consumed_unslotted_transition() const;
    bool has_consumed_state_tick(int64_t p_state_tick) const;
    int resolved_correction_mode() const;
    void record_server_input(int64_t p_tick, const godot::Dictionary &p_input);
    void finalize_recorded_state(const godot::Dictionary &p_payload);
};

} // namespace netw
