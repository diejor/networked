#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/prediction.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/slot_verdicts.hpp"
#include "netw/repl/property_binding.hpp"

namespace netw {

class NetwMultiplayer;
class NetwPrediction;

class NetwPredictSlotEngine {
    friend class NetwMultiplayer;
    friend class NetwPrediction;

    struct Declaration {
        uint64_t state = 0;
        uint64_t input = 0;
        bool authority = false;
        bool controlled_locally = false;
    };

    godot::ObjectID core_id;
    NetwPredictionEngine *pool = nullptr;
    int64_t seat = -1;
    godot::Ref<NetwEntity> entity;
    godot::Ref<NetwPrediction> handle;
    Declaration declaration;
    godot::Callable fallback_at_sink;
    godot::Dictionary unsampled;
    godot::PackedStringArray unparticipated;
    bool consume_primed = false;
    int64_t simulated_awaiting_step = -1;

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
    repl::PropertyBindingPtr state_binding() const;
    repl::PropertyBindingPtr input_binding() const;
    bool lane_open() const;
    double tick_delta() const;
    bool fallback_latched() const;
    int64_t schedule() const;
    int64_t ack() const;
    bool ack_advanced() const;

    int64_t route() const;
    predict::SlotCursors cursors() const;

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
        const predict::StateRow &p_payload
    );
    void begin_reseed(const predict::WritePlan &p_plan);
    void send_command_frame();
    void send_ack_frame();
    void file_command_cell(
        int64_t p_transition,
        const predict::StateRow &p_command,
        int p_origin
    );
    void judge_owner_claim(int64_t p_transition, int64_t p_fingerprint);
    int64_t recorded_transition() const;
    int declared_correction() const;
    int resolve_correction() const;
    int resolve_axes();
    int pool_island() const;
    void admit_reconcile_mode();
    void clear_island_promotions();
    sim::Mode resolved_mode() const;

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
    void prepare_tick_tape(int64_t p_tick);
    bool speculation_horizon_full();
    void publish_ack_age(int64_t p_age);
    void on_state(
        int64_t p_recv_tick,
        int64_t p_ack,
        const predict::Arrival *p_arrival,
        const predict::StateRow *p_held
    );

    void restore_row(
        const predict::StateRow &p_state,
        int p_operator,
        int64_t p_basis,
        NetwPredictSlotEngine *p_provenance_owner,
        int64_t p_entry
    );
    RecoveryPlan plan_of(const predict::WritePlan &p_plan) const;
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
    void note_joint_writes(const predict::StateRow &p_before);
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
    void consume_backlog(double p_delta, int p_buffer);

    void mark_idle_drive(int64_t p_label, int p_kind);
    void adopt_tape_position(int64_t p_transition);
    void author_command_entry(int64_t p_label, bool p_fresh);
    void refresh_tape_diagnostics();
    void run_command(
        const predict::StateRow &p_command,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );
    godot::Dictionary capture() const;
    void close_captured_row(int64_t p_transition);
    void close_journal_row(int64_t p_transition);
    godot::Dictionary witness_sample() const;
    godot::PackedStringArray island_participant_ids() const;
    void restore(
        const godot::Dictionary &p_payload,
        int p_operator,
        int64_t p_basis,
        NetwPredictSlotEngine *p_provenance_owner = nullptr,
        bool p_evidence_free = false,
        bool p_pool_planned = false,
        int64_t p_entry = -1
    );
    void register_with_loop();
    void unregister_from_loop();

    void on_control_changed(int64_t p_previous_peer, int64_t p_peer);
    void on_reparented();
    void on_state_frame(const predict::Arrival &p_arrival);
    void on_input_frame(const predict::Arrival &p_arrival);
    void on_simulated_state_frame(const predict::Arrival &p_arrival);
    void on_quarantine_state_frame(const predict::Arrival &p_arrival);
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
        const predict::StateRow &p_payload
    );
    void emit_reachability_findings(const godot::Array &p_findings);
    void maybe_demote_for_breach(
        int64_t p_transition,
        const godot::Dictionary &p_solve
    );
    godot::Ref<NetwPredictRecovery> recover_through_seam(
        const godot::Dictionary &p_carried,
        const godot::Dictionary &p_predicted,
        NetwPrediction::RecoveryPolicy p_policy,
        NetwSimulation::Restore p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_tier_errors,
        const godot::Dictionary &p_context,
        double p_tick_delta
    );
    godot::Ref<NetwPredictJudgement> evaluate_through_seam(
        NetwPrediction::Domain p_domain,
        NetwPrediction::ExactVerdict p_exact_verdict,
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

    int64_t order_key() const;
    void prepare_island(int p_schedule);
    void joint_pass(const NetwPredictTiming &p_timing);
    void network_tick(const NetwPredictTiming &p_timing);
    void simulate_tick(const NetwPredictTiming &p_timing);
    void simulate_frame(const NetwPredictTiming &p_timing);
    void simulate_stepped(const NetwPredictTiming &p_timing);
    void finalize_frame_state();
    void finalize_stepped_state();

    bool runs_in_pass(int p_pass_schedule) const;
    int64_t history_record_tick(int64_t p_fallback_tick) const;
    bool consumed_unslotted_transition() const;
    bool has_consumed_state_tick(int64_t p_state_tick) const;
    void finalize_recorded_state();
};

} // namespace netw
