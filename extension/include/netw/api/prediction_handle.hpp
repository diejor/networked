#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/api/predict_stats.hpp"

namespace netw {

class NetwPredictJournal;
class NetwPredictJudgement;
class NetwPredictRecovery;
class NetwPredictionEngine;
class NetwPredictSlotEngine;
class NetwReparentOpts;

class NetwPredictionHandle : public godot::RefCounted {
    GDCLASS(NetwPredictionHandle, godot::RefCounted)

    godot::Callable simulate_step;
    NetwPredict::Schedule schedule_value = NetwPredict::SCHEDULE_TICK;
    int correction_value = NetwPredict::CORRECTION_MODE_AUTO;
    int restore_value = NetwPredict::RESTORE_MODE_EXACT;
    int input_source_value = NetwPredict::INPUT_SOURCE_NONE;
    int sim_mode_value = NetwPredict::SIM_MODE_DISPLAY;
    int recovery_policy_value = -1;
    int breach_response_value = NetwPredict::BREACH_RESPONSE_PREDICT_THROUGH;
    godot::Ref<NetwPredictIsland> island_rule;
    godot::Dictionary sensor_table;
    int64_t epoch_value = -1;
    godot::Callable witness_sampler;
    godot::Callable corridor_sweep;
    int max_restore_value = 6;
    double teleport_value = 2.0;
    int cooldown_value = 6;
    bool sleeping_value = false;
    int missing_policy_value = NetwPredict::MISSING_INPUT_STALL;
    int max_consume_per_tick_value = 1;
    int consume_buffer_value = 0;
    int replay_buffer_value = 0;
    int max_consume_lag_value = 60;
    int ack_age_value = 0;
    double epsilon_value = 0.01;
    int reconcile_value = NetwPredict::RECONCILE_INDEPENDENT;
    NetwPredict::Archetype archetype_value = NetwPredict::ARCHETYPE_NONE;
    godot::Ref<NetwPredictStats> counters;

    godot::ObjectID entity_id;
    NetwPredictSlotEngine *engine_seat = nullptr;

    NetwPredictTiming frame_reading() const;
    NetwPredictSlotEngine *engine() const;
    NetwPredictionEngine *pool() const;
    int64_t slot() const;
    void reconfigure();
    void rewire();

protected:
    static void _bind_methods();

public:
    static const char *GENERATOR_UNKNOWN_BEYOND_RETENTION;

    NetwPredictionHandle();

    void bind_entity(const godot::Ref<NetwEntity> &p_entity);
    void bind_engine(NetwPredictSlotEngine *p_engine);

    void seat_control_changed(int64_t p_previous_peer, int64_t p_peer);
    void seat_state_frame(const godot::Dictionary &p_header);
    void seat_input_frame(const godot::Dictionary &p_header);
    void seat_simulated_state_frame(const godot::Dictionary &p_header);
    void seat_quarantine_state_frame(const godot::Dictionary &p_header);
    void seat_quarantine_witness(int64_t p_basis);
    void seat_deferred_operator_retry(int64_t p_basis);
    void seat_fallback_at(int64_t p_transition, int p_attribution);
    void seat_fallback(int64_t p_transition, int p_attribution, bool p_demoted);
    void seat_reseed_alignment(
        int64_t p_recv_tick,
        int64_t p_ack,
        const godot::Dictionary &p_payload
    );
    void seat_reachability_findings(const godot::Array &p_findings);
    void seat_breach(int64_t p_transition, const godot::Dictionary &p_solve);
    void seat_reconcile_mode();
    void seat_command_frame();
    godot::Ref<NetwPredictRecovery> seat_recover_seam(
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
    godot::Ref<NetwPredictJudgement> seat_evaluate_seam(
        NetwPredictJournal::Domain p_domain,
        NetwPredict::ExactVerdict p_exact_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_field_sink
    );

    godot::Ref<NetwEntity> get_entity() const;
    bool is_registered() const;

    void set_simulate(const godot::Callable &p_value);
    void set_schedule(NetwPredict::Schedule p_value);
    void set_correction_mode(int p_value);
    void set_snap_restore(int p_value);
    void set_input_source(int p_value);
    void set_sim_mode(int p_value);
    void set_recovery_policy(int p_value);
    void set_breach_response(int p_value);
    void set_island(const godot::Ref<NetwPredictIsland> &p_value);
    void set_sensors(const godot::Dictionary &p_value);
    void set_epoch(int64_t p_value);
    void set_witness_contacts(const godot::Callable &p_value);
    void set_transport_corridor(const godot::Callable &p_value);
    void set_max_restore_ticks(int p_value);
    void set_teleport_threshold(double p_value);
    void set_collision_cooldown_ticks(int p_value);
    void set_sleeping(bool p_value);
    void set_missing_policy(int p_value);
    void set_max_consume_per_tick(int p_value);
    void set_consume_buffer_ticks(int p_value);
    void set_replay_buffer_depth(int p_value);
    void set_max_consume_lag_ticks(int p_value);
    void set_ack_age_ticks(int p_value);
    void set_divergence_epsilon(double p_value);
    void set_reconcile_mode(int p_value);
    void set_archetype(NetwPredict::Archetype p_value);

    godot::Callable get_simulate() const {
        return simulate_step;
    }

    NetwPredict::Schedule get_schedule() const {
        return schedule_value;
    }

    int get_correction_mode() const {
        return correction_value;
    }

    int get_snap_restore() const {
        return restore_value;
    }

    int get_input_source() const {
        return input_source_value;
    }

    int get_sim_mode() const {
        return sim_mode_value;
    }

    int get_recovery_policy() const {
        return recovery_policy_value;
    }

    int get_breach_response() const {
        return breach_response_value;
    }

    godot::Ref<NetwPredictIsland> get_island() const {
        return island_rule;
    }

    godot::Dictionary get_sensors() const {
        return sensor_table;
    }

    int64_t get_epoch() const {
        return epoch_value;
    }

    godot::Callable get_witness_contacts() const {
        return witness_sampler;
    }

    godot::Callable get_transport_corridor() const {
        return corridor_sweep;
    }

    int get_max_restore_ticks() const {
        return max_restore_value;
    }

    double get_teleport_threshold() const {
        return teleport_value;
    }

    int get_collision_cooldown_ticks() const {
        return cooldown_value;
    }

    bool get_sleeping() const {
        return sleeping_value;
    }

    int get_missing_policy() const {
        return missing_policy_value;
    }

    int get_max_consume_per_tick() const {
        return max_consume_per_tick_value;
    }

    int get_consume_buffer_ticks() const {
        return consume_buffer_value;
    }

    int get_replay_buffer_depth() const {
        return replay_buffer_value;
    }

    int get_max_consume_lag_ticks() const {
        return max_consume_lag_value;
    }

    int get_ack_age_ticks() const {
        return ack_age_value;
    }

    double get_divergence_epsilon() const {
        return epsilon_value;
    }

    int get_reconcile_mode() const {
        return reconcile_value;
    }

    NetwPredict::Archetype get_archetype() const {
        return archetype_value;
    }

    godot::Ref<NetwPredictStats> get_stats() const {
        return counters;
    }

    godot::Dictionary get_last_field_divergence() const;
    godot::Dictionary get_field_recovery() const;
    int get_last_compare_staleness() const;
    godot::Dictionary get_last_tier_errors() const;
    int get_last_verdict_reason() const;
    bool get_is_reconciling() const;
    int64_t get_acknowledged_tick() const;
    int get_last_attribution() const;
    int64_t get_last_attributed_transition() const;

    godot::Variant sensor(
        const godot::StringName &p_name,
        const godot::Variant &p_default = godot::Variant()
    ) const;
    NetwPredict::RecoveryPolicy resolved_recovery_policy() const;
    NetwPredict::CorrectionMode resolved_correction_mode() const;

    void simulate_tick(double p_delta, int64_t p_tick);
    void simulate_frame(double p_delta);

    godot::Ref<NetwPredictJournal> journal() const;
    godot::Dictionary teleport_distances() const;
    godot::Dictionary reachability() const;
    godot::Dictionary episode() const;
    godot::Dictionary episode_digest() const;
    godot::TypedArray<godot::Dictionary> tape_transitions() const;
    godot::Dictionary transition_state_at(int64_t p_transition) const;
    void record_server_input(int64_t p_tick, const godot::Dictionary &p_input);
    bool has_consumed_state_tick(int64_t p_state_tick) const;
    void notify_contact();
    int64_t history_record_tick(int64_t p_fallback) const;

    void stamp_episode();
    void set_simulated_by(
        const godot::Ref<NetwEntity> &p_subject,
        bool p_enabled,
        const godot::Callable &p_predictor = godot::Callable()
    );
    int simulated_by_count() const;
    godot::Callable predicted_command_callable() const;
    void restate_declaration();

    static NetwPredict::Role role_for_axes(
        NetwPredict::InputSource p_source,
        NetwPredict::SimMode p_mode
    );
    static NetwPredict::CorrectionMode resolve_correction_mode_for(
        godot::Object *p_body,
        NetwPredict::CorrectionMode p_mode,
        bool p_solves = false
    );
    static double divergence(
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authoritative,
        const godot::Dictionary &p_angles = godot::Dictionary()
    );
    static double divergence_by_field(
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authoritative,
        godot::Dictionary p_out,
        const godot::Dictionary &p_angles = godot::Dictionary()
    );
    static bool triggers(double p_error, double p_tolerance);
    static bool diverged(
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_authoritative,
        double p_epsilon,
        const godot::Dictionary &p_overrides,
        const godot::Dictionary &p_excludes = godot::Dictionary(),
        const godot::Dictionary &p_angles = godot::Dictionary()
    );
    static double value_error(
        const godot::Variant &p_a,
        const godot::Variant &p_b
    );
    static double field_error(
        const godot::Variant &p_a,
        const godot::Variant &p_b,
        bool p_is_angle
    );
};

godot::Ref<NetwPredictionHandle> build_prediction_handle(
    godot::Object *p_entity
);

} // namespace netw
