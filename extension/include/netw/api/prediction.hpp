#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/simulation.hpp"
#include "netw/predict/arrival.hpp"

namespace netw {

class NetwPredictJudgement;
class NetwPredictRecovery;
class NetwPredictionEngine;
class NetwPredictSlotEngine;

class NetwPrediction : public godot::RefCounted {
    GDCLASS(NetwPrediction, godot::RefCounted)

public:
    static constexpr double DEFAULT_TELEPORT_THRESHOLD = 2.0;
    static constexpr int ACK_AGE_MAX = 64;

    enum Role {
        ROLE_PREDICT = 0,
        ROLE_CONSUME = 1,
        ROLE_HOST_LOCAL = 2,
        ROLE_REMOTE = 3,
        ROLE_SIMULATE = 4,
    };

    enum InputSource {
        INPUT_SOURCE_LOCAL = 0,
        INPUT_SOURCE_RECEIVED = 1,
        INPUT_SOURCE_PREDICTED = 2,
        INPUT_SOURCE_NONE = 3,
    };

    enum SimMode {
        SIM_MODE_AUTHORITATIVE = 0,
        SIM_MODE_SPECULATIVE = 1,
        SIM_MODE_DISPLAY = 2,
    };

    enum ContactClass {
        CONTACT_CLASS_NONE = 0,
        CONTACT_CLASS_DECLARED_SUPPORT = 1,
        CONTACT_CLASS_OTHER_STATIC = 2,
        CONTACT_CLASS_PREDICTED_DYNAMIC = 3,
        CONTACT_CLASS_UNPREDICTED_DYNAMIC = 4,
        CONTACT_CLASS_KINEMATIC_PROXY = 5,
    };

    enum CommandOrigin {
        COMMAND_ORIGIN_PREDICTED = 0,
        COMMAND_ORIGIN_RELAYED = 1,
    };

    enum DriveKind : int {
        DRIVE_KIND_NONE = 0,
        DRIVE_KIND_FRESH = 1,
        DRIVE_KIND_REPEAT = 2,
        DRIVE_KIND_HOLD = 3,
        DRIVE_KIND_STARVED = 4,
        DRIVE_KIND_FOLD_DRIVE = 5,
        DRIVE_KIND_MISSING = 6,
        DRIVE_KIND_SUBSTITUTED = 7,
    };

    enum TriggerShape {
        TRIGGER_SHAPE_NONE = 0,
        TRIGGER_SHAPE_MIXED = 1,
        TRIGGER_SHAPE_ALL_WITHHELD = 2,
    };

    enum ConsumeAction {
        CONSUME_ACTION_REPLAY = 0,
        CONSUME_ACTION_HOLD = 1,
        CONSUME_ACTION_STARVED = 2,
    };

    enum ExactVerdict {
        EXACT_VERDICT_UNJUDGED = 0,
        EXACT_VERDICT_EQUAL = 1,
        EXACT_VERDICT_UNEQUAL = 2,
    };

    enum MissingInput {
        MISSING_INPUT_STALL = 0,
        MISSING_INPUT_REPEAT_LAST = 1,
    };

    enum Archetype {
        ARCHETYPE_NONE = 0,
        ARCHETYPE_SCRIPTED = 1,
        ARCHETYPE_SOLVER_BODY = 2,
    };

    enum RecoveryPolicy {
        RECOVERY_POLICY_REBASE_REPLAY = 0,
        RECOVERY_POLICY_REBASE_RECOVER = 1,
        RECOVERY_POLICY_DELAY_CLOSED = 2,
        RECOVERY_POLICY_OBSERVE = 3,
        RECOVERY_POLICY_AUTO = 4,
    };

    enum Reconcile {
        RECONCILE_INDEPENDENT = 0,
        RECONCILE_JOINT = 1,
    };

    enum BreachResponse {
        BREACH_RESPONSE_PREDICT_THROUGH = 0,
        BREACH_RESPONSE_DEMOTE = 1,
    };

    enum EpisodeState {
        EPISODE_STATE_OPEN = 0,
        EPISODE_STATE_CLOSED = 1,
        EPISODE_STATE_FALLBACK = 2,
    };

    enum EpisodeChange {
        EPISODE_CHANGE_OPENED = 0,
        EPISODE_CHANGE_FALLBACK = 1,
        EPISODE_CHANGE_CLOSED = 2,
    };

    enum OperatorOutcome {
        OPERATOR_OUTCOME_PENDING = 0,
        OPERATOR_OUTCOME_CONTRACTED = 1,
        OPERATOR_OUTCOME_FAILED_TO_CONTRACT = 2,
        OPERATOR_OUTCOME_INTRODUCED_BOUNDARY = 3,
        OPERATOR_OUTCOME_WITHHELD = 4,
    };

    enum Attribution {
        ATTRIBUTION_UNKNOWN = 0,
        ATTRIBUTION_PRE_STATE = 1,
        ATTRIBUTION_COMMAND = 2,
        ATTRIBUTION_ENVIRONMENT = 3,
        ATTRIBUTION_TOPOLOGY = 4,
        ATTRIBUTION_EXECUTION = 5,
        ATTRIBUTION_CONTACT = 6,
        ATTRIBUTION_CLOSURE = 7,
    };

    enum Operator {
        OPERATOR_NONE = 0,
        OPERATOR_REBASE_PROJECTED = 1,
        OPERATOR_REBASE_EXACT = 2,
        OPERATOR_FULL_CLOSURE = 3,
        OPERATOR_TRANSPORT_DELTA = 4,
        OPERATOR_RESEED = 5,
        OPERATOR_RESEED_ALIGN = 6,
        OPERATOR_DEMOTE = 7,
        OPERATOR_DISSIPATE = 8,
        OPERATOR_JOINT_REBASE = 9,
    };

    enum Domain {
        DOMAIN_IN = 0,
        DOMAIN_OUT = 1,
    };

private:
    int input_source_value = NetwPrediction::INPUT_SOURCE_NONE;
    int sim_mode_value = NetwPrediction::SIM_MODE_DISPLAY;
    int recovery_policy_value = NetwPrediction::RECOVERY_POLICY_AUTO;
    int breach_response_value = NetwPrediction::BREACH_RESPONSE_PREDICT_THROUGH;
    godot::Callable command_predictor;
    godot::Dictionary sensor_table;
    int64_t epoch_value = -1;
    godot::Callable witness_sampler;
    godot::Callable corridor_sweep;
    double teleport_value = DEFAULT_TELEPORT_THRESHOLD;
    int cooldown_value = 6;
    bool sleeping_value = false;
    int missing_policy_value = NetwPrediction::MISSING_INPUT_STALL;
    int max_consume_per_tick_value = 1;
    int consume_buffer_value = 0;
    int replay_buffer_value = 0;
    int max_consume_lag_value = 60;
    int ack_age_value = 0;
    double epsilon_value = 0.01;
    int reconcile_value = NetwPrediction::RECONCILE_INDEPENDENT;
    NetwPrediction::Archetype archetype_value = NetwPrediction::ARCHETYPE_NONE;
    NetwPredictStats::Book facts;
    mutable godot::Ref<NetwPredictStats> counters;

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

    NetwPrediction() = default;
    ~NetwPrediction();

    void bind_entity(const godot::Ref<NetwEntity> &p_entity);
    void bind_engine(NetwPredictSlotEngine *p_engine);

    void seat_control_changed(int64_t p_previous_peer, int64_t p_peer);
    void seat_arrival(
        predict::Listener p_listener,
        const predict::Arrival &p_arrival
    );
    void seat_quarantine_witness(int64_t p_basis);
    void seat_deferred_operator_retry(int64_t p_basis);
    void seat_fallback_at(int64_t p_transition, int p_attribution);
    void seat_fallback(int64_t p_transition, int p_attribution, bool p_demoted);
    void seat_reachability_findings(const godot::Array &p_findings);
    void seat_breach(int64_t p_transition, const godot::Dictionary &p_solve);
    void seat_reconcile_mode();
    void seat_command_frame();
    godot::Ref<NetwPredictRecovery> seat_recover_seam(
        const godot::Dictionary &p_carried,
        NetwPrediction::RecoveryPolicy p_policy,
        NetwSimulation::Restore p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_before,
        const godot::Dictionary &p_tier_errors,
        const godot::Dictionary &p_context,
        double p_tick_delta
    );
    godot::Ref<NetwPredictJudgement> seat_evaluate_seam(
        NetwPrediction::Domain p_domain,
        NetwPrediction::ExactVerdict p_exact_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_field_sink
    );

    godot::Ref<NetwEntity> get_entity() const;
    bool is_registered() const;

    godot::Ref<NetwSimulation> simulation() const;
    void step_changed();
    void set_input_source(NetwPrediction::InputSource p_value);
    void set_sim_mode(NetwPrediction::SimMode p_value);
    void set_recovery_policy(NetwPrediction::RecoveryPolicy p_value);
    void set_breach_response(NetwPrediction::BreachResponse p_value);
    void set_sensors(const godot::Dictionary &p_value);
    void set_epoch(int64_t p_value);
    void set_witness_contacts(const godot::Callable &p_value);
    void set_correction_path_clear(const godot::Callable &p_value);
    void set_teleport_threshold(double p_value);
    void set_collision_cooldown_ticks(int p_value);
    void set_sleeping(bool p_value);
    void set_missing_policy(NetwPrediction::MissingInput p_value);
    void set_max_consume_per_tick(int p_value);
    void set_consume_buffer_ticks(int p_value);
    void set_replay_buffer_depth(int p_value);
    void set_max_consume_lag_ticks(int p_value);
    void set_ack_age_ticks(int p_value);
    void set_divergence_epsilon(double p_value);
    void set_reconcile_mode(NetwPrediction::Reconcile p_value);
    void set_archetype(NetwPrediction::Archetype p_value);

    godot::Callable get_simulate() const;
    NetwSimulation::Schedule get_schedule() const;
    NetwSimulation::Restore get_snap_restore() const;
    int get_max_restore_ticks() const;

    NetwPrediction::InputSource get_input_source() const {
        return static_cast<NetwPrediction::InputSource>(input_source_value);
    }

    NetwPrediction::SimMode get_sim_mode() const {
        return static_cast<NetwPrediction::SimMode>(sim_mode_value);
    }

    NetwPrediction::RecoveryPolicy get_recovery_policy() const {
        return static_cast<NetwPrediction::RecoveryPolicy>(
            recovery_policy_value
        );
    }

    NetwPrediction::BreachResponse get_breach_response() const {
        return static_cast<NetwPrediction::BreachResponse>(
            breach_response_value
        );
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

    godot::Callable get_correction_path_clear() const {
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

    NetwPrediction::MissingInput get_missing_policy() const {
        return static_cast<NetwPrediction::MissingInput>(missing_policy_value);
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

    NetwPrediction::Reconcile get_reconcile_mode() const {
        return static_cast<NetwPrediction::Reconcile>(reconcile_value);
    }

    NetwPrediction::Archetype get_archetype() const {
        return archetype_value;
    }

    godot::Ref<NetwPredictStats> get_stats() const;

    NetwPredictStats::Book *fact_book() {
        return &facts;
    }

    bool stats_minted() const {
        return counters.is_valid();
    }

    godot::Variant sensor(
        const godot::StringName &p_name,
        const godot::Variant &p_default = godot::Variant()
    ) const;
    NetwPrediction::RecoveryPolicy resolved_recovery_policy() const;
    int resolved_correction() const;
    bool body_solves() const;

    void simulate_tick(double p_delta, int64_t p_tick);
    void simulate_frame(double p_delta);

    godot::Dictionary reachability() const;
    void notify_contact();

    void set_predict_input(const godot::Callable &p_predictor);

    godot::Callable get_predict_input() const {
        return command_predictor;
    }

    void restate_declaration();
};

godot::Ref<NetwPrediction> build_prediction_handle(
    godot::Object *p_entity
);

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPrediction::ContactClass);
VARIANT_ENUM_CAST(netw::NetwPrediction::CommandOrigin);
VARIANT_ENUM_CAST(netw::NetwPrediction::DriveKind);
VARIANT_ENUM_CAST(netw::NetwPrediction::TriggerShape);
VARIANT_ENUM_CAST(netw::NetwPrediction::ConsumeAction);
VARIANT_ENUM_CAST(netw::NetwPrediction::ExactVerdict);
VARIANT_ENUM_CAST(netw::NetwPrediction::MissingInput);
VARIANT_ENUM_CAST(netw::NetwPrediction::Archetype);
VARIANT_ENUM_CAST(netw::NetwPrediction::RecoveryPolicy);
VARIANT_ENUM_CAST(netw::NetwPrediction::Reconcile);
VARIANT_ENUM_CAST(netw::NetwPrediction::BreachResponse);
VARIANT_ENUM_CAST(netw::NetwPrediction::EpisodeState);
VARIANT_ENUM_CAST(netw::NetwPrediction::EpisodeChange);
VARIANT_ENUM_CAST(netw::NetwPrediction::OperatorOutcome);
VARIANT_ENUM_CAST(netw::NetwPrediction::Attribution);
VARIANT_ENUM_CAST(netw::NetwPrediction::Operator);
VARIANT_ENUM_CAST(netw::NetwPrediction::Domain);
