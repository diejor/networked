#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/simulation_handle.hpp"

namespace netw {

class NetwPredictJudgement;
class NetwPredictRecovery;
class NetwPredictionEngine;
class NetwPredictSlotEngine;

class NetwPredictionHandle : public godot::RefCounted {
    GDCLASS(NetwPredictionHandle, godot::RefCounted)

public:
    static constexpr double DEFAULT_TELEPORT_THRESHOLD = 2.0;

private:
    int input_source_value = NetwPredict::INPUT_SOURCE_NONE;
    int sim_mode_value = NetwPredict::SIM_MODE_DISPLAY;
    int recovery_policy_value = NetwPredict::RECOVERY_POLICY_AUTO;
    int breach_response_value = NetwPredict::BREACH_RESPONSE_PREDICT_THROUGH;
    godot::Callable command_predictor;
    godot::Dictionary sensor_table;
    int64_t epoch_value = -1;
    godot::Callable witness_sampler;
    godot::Callable corridor_sweep;
    double teleport_value = DEFAULT_TELEPORT_THRESHOLD;
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
        NetwSimulationHandle::Restore p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_tier_errors,
        const godot::Dictionary &p_context,
        double p_tick_delta
    );
    godot::Ref<NetwPredictJudgement> seat_evaluate_seam(
        NetwPredict::Domain p_domain,
        NetwPredict::ExactVerdict p_exact_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_field_sink
    );

    godot::Ref<NetwEntity> get_entity() const;
    bool is_registered() const;

    godot::Ref<NetwSimulationHandle> simulation() const;
    void step_changed();
    void set_input_source(NetwPredict::InputSource p_value);
    void set_sim_mode(NetwPredict::SimMode p_value);
    void set_recovery_policy(NetwPredict::RecoveryPolicy p_value);
    void set_breach_response(NetwPredict::BreachResponse p_value);
    void set_sensors(const godot::Dictionary &p_value);
    void set_epoch(int64_t p_value);
    void set_witness_contacts(const godot::Callable &p_value);
    void set_transport_corridor(const godot::Callable &p_value);
    void set_teleport_threshold(double p_value);
    void set_collision_cooldown_ticks(int p_value);
    void set_sleeping(bool p_value);
    void set_missing_policy(NetwPredict::MissingInput p_value);
    void set_max_consume_per_tick(int p_value);
    void set_consume_buffer_ticks(int p_value);
    void set_replay_buffer_depth(int p_value);
    void set_max_consume_lag_ticks(int p_value);
    void set_ack_age_ticks(int p_value);
    void set_divergence_epsilon(double p_value);
    void set_reconcile_mode(NetwPredict::Reconcile p_value);
    void set_archetype(NetwPredict::Archetype p_value);

    godot::Callable get_simulate() const;
    NetwSimulationHandle::Schedule get_schedule() const;
    NetwSimulationHandle::Restore get_snap_restore() const;
    int get_max_restore_ticks() const;

    NetwPredict::InputSource get_input_source() const {
        return static_cast<NetwPredict::InputSource>(input_source_value);
    }

    NetwPredict::SimMode get_sim_mode() const {
        return static_cast<NetwPredict::SimMode>(sim_mode_value);
    }

    NetwPredict::RecoveryPolicy get_recovery_policy() const {
        return static_cast<NetwPredict::RecoveryPolicy>(recovery_policy_value);
    }

    NetwPredict::BreachResponse get_breach_response() const {
        return static_cast<NetwPredict::BreachResponse>(breach_response_value);
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

    double get_teleport_threshold() const {
        return teleport_value;
    }

    int get_collision_cooldown_ticks() const {
        return cooldown_value;
    }

    bool get_sleeping() const {
        return sleeping_value;
    }

    NetwPredict::MissingInput get_missing_policy() const {
        return static_cast<NetwPredict::MissingInput>(missing_policy_value);
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

    NetwPredict::Reconcile get_reconcile_mode() const {
        return static_cast<NetwPredict::Reconcile>(reconcile_value);
    }

    NetwPredict::Archetype get_archetype() const {
        return archetype_value;
    }

    godot::Ref<NetwPredictStats> get_stats() const {
        return counters;
    }

    godot::Variant sensor(
        const godot::StringName &p_name,
        const godot::Variant &p_default = godot::Variant()
    ) const;
    NetwPredict::RecoveryPolicy resolved_recovery_policy() const;
    int resolved_correction() const;
    bool body_solves() const;

    void simulate_tick(double p_delta, int64_t p_tick);
    void simulate_frame(double p_delta);

    godot::Dictionary reachability() const;
    void notify_contact();

    void set_predict_commands(const godot::Callable &p_predictor);

    godot::Callable get_predict_commands() const {
        return command_predictor;
    }

    void restate_declaration();
};

godot::Ref<NetwPredictionHandle> build_prediction_handle(
    godot::Object *p_entity
);

} // namespace netw
