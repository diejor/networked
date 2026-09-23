#include "netw/api/prediction_handle.hpp"

#include <algorithm>
#include <cmath>

#include "godot/class_db.hpp"
#include "godot/math.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/log.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"

using namespace godot;

namespace netw {

const char *NetwPredictionHandle::GENERATOR_UNKNOWN_BEYOND_RETENTION
    = "UNKNOWN_BEYOND_RETENTION";

NetwPredictionHandle::NetwPredictionHandle() {
    counters.instantiate();
}

NetwPredictTiming NetwPredictionHandle::frame_reading() const {
    NetwPredictSlotEngine *held = engine();
    NetwMultiplayer *session = held == nullptr ? nullptr : held->core();
    if (session != nullptr) {
        return session->frame_timing();
    }
    return NetwPredictTiming::of(0, 0.0, 0.0);
}

NetwPredictSlotEngine *NetwPredictionHandle::engine() const {
    return engine_seat;
}

NetwPredictionEngine *NetwPredictionHandle::pool() const {
    NetwPredictSlotEngine *held = engine();
    return held == nullptr ? nullptr : held->seated_pool();
}

int64_t NetwPredictionHandle::slot() const {
    NetwPredictSlotEngine *held = engine();
    return held == nullptr ? -1 : held->native_slot();
}

void NetwPredictionHandle::reconfigure() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->reconfigure();
    }
}

void NetwPredictionHandle::rewire() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->rewire();
    }
}

void NetwPredictionHandle::bind_entity(const Ref<NetwEntity> &p_entity) {
    entity_id = p_entity.is_valid() ? p_entity->get_instance_id() : ObjectID();
}

void NetwPredictionHandle::bind_engine(NetwPredictSlotEngine *p_engine) {
    engine_seat = p_engine;
}

void NetwPredictionHandle::seat_control_changed(
    int64_t p_previous_peer,
    int64_t p_peer
) {
    if (engine_seat != nullptr) {
        engine_seat->on_control_changed(p_previous_peer, p_peer);
    }
}

void NetwPredictionHandle::seat_state_frame(const Dictionary &p_header) {
    if (engine_seat != nullptr) {
        engine_seat->on_state_frame(p_header);
    }
}

void NetwPredictionHandle::seat_input_frame(const Dictionary &p_header) {
    if (engine_seat != nullptr) {
        engine_seat->on_input_frame(p_header);
    }
}

void NetwPredictionHandle::seat_simulated_state_frame(
    const Dictionary &p_header
) {
    if (engine_seat != nullptr) {
        engine_seat->on_simulated_state_frame(p_header);
    }
}

void NetwPredictionHandle::seat_quarantine_state_frame(
    const Dictionary &p_header
) {
    if (engine_seat != nullptr) {
        engine_seat->on_quarantine_state_frame(p_header);
    }
}

void NetwPredictionHandle::seat_quarantine_witness(int64_t p_basis) {
    if (engine_seat != nullptr) {
        engine_seat->apply_quarantine_witness(p_basis);
    }
}

void NetwPredictionHandle::seat_deferred_operator_retry(int64_t p_basis) {
    if (engine_seat != nullptr) {
        engine_seat->retry_deferred_operator(p_basis);
    }
}

void NetwPredictionHandle::seat_fallback_at(
    int64_t p_transition,
    int p_attribution
) {
    if (engine_seat != nullptr) {
        engine_seat->enter_fallback_at(p_transition, p_attribution);
    }
}

void NetwPredictionHandle::seat_fallback(
    int64_t p_transition,
    int p_attribution,
    bool p_demoted
) {
    if (engine_seat != nullptr) {
        engine_seat->enter_fallback(p_transition, p_attribution, p_demoted);
    }
}

void NetwPredictionHandle::seat_reseed_alignment(
    int64_t p_recv_tick,
    int64_t p_ack,
    const Dictionary &p_payload
) {
    if (engine_seat != nullptr) {
        engine_seat->finish_reseed_alignment(p_recv_tick, p_ack, p_payload);
    }
}

void NetwPredictionHandle::seat_reachability_findings(const Array &p_findings) {
    if (engine_seat != nullptr) {
        engine_seat->emit_reachability_findings(p_findings);
    }
}

void NetwPredictionHandle::seat_breach(
    int64_t p_transition,
    const Dictionary &p_solve
) {
    if (engine_seat != nullptr) {
        engine_seat->maybe_demote_for_breach(p_transition, p_solve);
    }
}

void NetwPredictionHandle::seat_reconcile_mode() {
    if (engine_seat != nullptr) {
        engine_seat->admit_reconcile_mode();
    }
}

void NetwPredictionHandle::seat_command_frame() {
    if (engine_seat != nullptr) {
        engine_seat->send_command_frame();
    }
}

Ref<NetwPredictRecovery> NetwPredictionHandle::seat_recover_seam(
    const Dictionary &p_carried,
    NetwPredict::RecoveryPolicy p_policy,
    NetwSimulationHandle::Restore p_snap_restore,
    const Dictionary &p_projection,
    const Dictionary &p_before,
    const Dictionary &p_tier_errors,
    const Dictionary &p_context,
    double p_tick_delta
) {
    return engine_seat == nullptr ? Ref<NetwPredictRecovery>()
                                  : engine_seat->recover_through_seam(
                                        p_carried,
                                        p_policy,
                                        p_snap_restore,
                                        p_projection,
                                        p_before,
                                        p_tier_errors,
                                        p_context,
                                        p_tick_delta
                                    );
}

Ref<NetwPredictJudgement> NetwPredictionHandle::seat_evaluate_seam(
    NetwPredict::Domain p_domain,
    NetwPredict::ExactVerdict p_exact_verdict,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    const Dictionary &p_field_sink
) {
    return engine_seat == nullptr ? Ref<NetwPredictJudgement>()
                                  : engine_seat->evaluate_through_seam(
                                        p_domain,
                                        p_exact_verdict,
                                        p_predicted,
                                        p_payload,
                                        p_field_sink
                                    );
}

Ref<NetwEntity> NetwPredictionHandle::get_entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(ObjectDB::get_instance(entity_id))
    );
}

bool NetwPredictionHandle::is_registered() const {
    return engine() != nullptr;
}

Ref<NetwSimulationHandle> NetwPredictionHandle::simulation() const {
    const Ref<NetwEntity> bound = get_entity();
    return bound.is_valid() ? bound->get_simulation()
                            : Ref<NetwSimulationHandle>();
}

void NetwPredictionHandle::step_changed() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->push_simulate();
    }
}

Callable NetwPredictionHandle::get_simulate() const {
    const Ref<NetwSimulationHandle> held = simulation();
    return held.is_valid() ? held->get_step() : Callable();
}

NetwSimulationHandle::Schedule NetwPredictionHandle::get_schedule() const {
    const Ref<NetwSimulationHandle> held = simulation();
    return held.is_valid() ? held->resolved_schedule()
                           : NetwSimulationHandle::SCHEDULE_TICK;
}

NetwSimulationHandle::Restore NetwPredictionHandle::get_snap_restore() const {
    const Ref<NetwSimulationHandle> held = simulation();
    return held.is_valid()
            && held->get_restore() == NetwSimulationHandle::RESTORE_EXTRAPOLATED
        ? NetwSimulationHandle::RESTORE_EXTRAPOLATED
        : NetwSimulationHandle::RESTORE_EXACT;
}

int NetwPredictionHandle::get_max_restore_ticks() const {
    const Ref<NetwSimulationHandle> held = simulation();
    return held.is_valid() ? held->get_max_restore_ticks() : 6;
}

void NetwPredictionHandle::set_input_source(NetwPredict::InputSource p_value) {
    input_source_value = p_value;
}

void NetwPredictionHandle::set_sim_mode(NetwPredict::SimMode p_value) {
    sim_mode_value = p_value;
}

void NetwPredictionHandle::set_recovery_policy(
    NetwPredict::RecoveryPolicy p_value
) {
    recovery_policy_value = p_value;
    reconfigure();
    rewire();
}

void NetwPredictionHandle::set_breach_response(
    NetwPredict::BreachResponse p_value
) {
    breach_response_value = p_value;
    NetwPredictionEngine *held = pool();
    if (held != nullptr) {
        held->note_breach_source(slot(), StringName("code"));
    }
}

void NetwPredictionHandle::set_sensors(const Dictionary &p_value) {
    sensor_table = p_value;
}

void NetwPredictionHandle::set_epoch(int64_t p_value) {
    epoch_value = p_value;
    NetwPredictionEngine *held = pool();
    if (held != nullptr) {
        held->set_declared_epoch(slot(), p_value);
    }
}

void NetwPredictionHandle::set_witness_contacts(const Callable &p_value) {
    NETW_ERR_COND(
        !p_value.is_null() && !p_value.is_valid(),
        sys::PREDICTION,
        "PredictionHandle.witness_contacts: sampler must be a valid Callable. "
        "The witness boundary stays unknown."
    );
    witness_sampler = p_value;
    rewire();
}

void NetwPredictionHandle::set_transport_corridor(const Callable &p_value) {
    NETW_ERR_COND(
        !p_value.is_null() && !p_value.is_valid(),
        sys::PREDICTION,
        "PredictionHandle.transport_corridor: sweep must be a valid Callable. "
        "Transport stays disabled."
    );
    corridor_sweep = p_value;
}

void NetwPredictionHandle::set_teleport_threshold(double p_value) {
    teleport_value = std::max(0.0, p_value);
    reconfigure();
}

void NetwPredictionHandle::set_collision_cooldown_ticks(int p_value) {
    cooldown_value = std::max(0, p_value);
    reconfigure();
}

void NetwPredictionHandle::set_sleeping(bool p_value) {
    sleeping_value = p_value;
}

void NetwPredictionHandle::set_missing_policy(
    NetwPredict::MissingInput p_value
) {
    missing_policy_value = p_value;
}

void NetwPredictionHandle::set_max_consume_per_tick(int p_value) {
    max_consume_per_tick_value = p_value;
}

void NetwPredictionHandle::set_consume_buffer_ticks(int p_value) {
    consume_buffer_value = p_value;
}

void NetwPredictionHandle::set_replay_buffer_depth(int p_value) {
    replay_buffer_value = std::max(0, p_value);
}

void NetwPredictionHandle::set_max_consume_lag_ticks(int p_value) {
    max_consume_lag_value = std::max(0, p_value);
}

void NetwPredictionHandle::set_ack_age_ticks(int p_value) {
    ack_age_value = p_value;
}

void NetwPredictionHandle::set_divergence_epsilon(double p_value) {
    epsilon_value = std::max(0.0, p_value);
    reconfigure();
}

void NetwPredictionHandle::set_reconcile_mode(NetwPredict::Reconcile p_value) {
    reconcile_value = p_value;
    reconfigure();
}

void NetwPredictionHandle::set_archetype(NetwPredict::Archetype p_value) {
    archetype_value = p_value;
    const Dictionary axes = NetwPredictionEngine::archetype_axes(p_value);
    if (!bool(axes[StringName("declared")])) {
        return;
    }
    const Ref<NetwSimulationHandle> held = simulation();
    if (held.is_valid()) {
        held->preset(
            NetwSimulationHandle::Schedule(int(axes[StringName("schedule")])),
            bool(axes[StringName("declares_snap_restore")]),
            NetwSimulationHandle::Restore(int(axes[StringName("snap_restore")]))
        );
    }
    set_missing_policy(static_cast<NetwPredict::MissingInput>(
        int(axes[StringName("missing_policy")])
    ));
    set_recovery_policy(static_cast<NetwPredict::RecoveryPolicy>(
        int(axes[StringName("recovery_policy")])
    ));
    if (bool(axes[StringName("declares_teleport_threshold")])) {
        set_teleport_threshold(double(axes[StringName("teleport_threshold")]));
    }
}

Variant NetwPredictionHandle::sensor(
    const StringName &p_name,
    const Variant &p_default
) const {
    NetwPredictionEngine *held = pool();
    if (held == nullptr) {
        return p_default;
    }
    return held->sensor_samples(slot()).get(p_name, p_default);
}

NetwPredict::RecoveryPolicy NetwPredictionHandle::
    resolved_recovery_policy() const {
    const bool solves = body_solves();
    return static_cast<NetwPredict::RecoveryPolicy>(
        predict::integrable_recovery_policy(
            predict::resolve_recovery_policy(recovery_policy_value, solves),
            get_schedule(),
            solves
        )
    );
}

int NetwPredictionHandle::resolved_correction() const {
    return predict::correction_for_recovery_policy(resolved_recovery_policy());
}

bool NetwPredictionHandle::body_solves() const {
    NetwPredictionEngine *held = pool();
    if (held != nullptr && held->owner_bound(slot())) {
        return held->owner_solves(slot());
    }
    const Ref<NetwEntity> bound = get_entity();
    Object *body = bound.is_valid() ? bound->get_owner() : nullptr;
    return Object::cast_to<RigidBody2D>(body) != nullptr
        || Object::cast_to<RigidBody3D>(body) != nullptr;
}

void NetwPredictionHandle::simulate_tick(double p_delta, int64_t p_tick) {
    NetwPredictSlotEngine *held = engine();
    if (held == nullptr) {
        return;
    }
    const NetwPredictTiming reading = frame_reading();
    held->simulate_tick(
        NetwPredictTiming::of(p_tick, p_delta, reading.get_ticktime())
    );
}

void NetwPredictionHandle::simulate_frame(double p_delta) {
    NetwPredictSlotEngine *held = engine();
    if (held == nullptr) {
        return;
    }
    const NetwPredictTiming reading = frame_reading();
    held->simulate_frame(
        NetwPredictTiming::of(
            reading.get_tick(),
            p_delta,
            reading.get_ticktime(),
            reading.get_frame(),
            reading.get_quantum(),
            reading.get_simulating()
        )
    );
}

Dictionary NetwPredictionHandle::reachability() const {
    NetwPredictionEngine *held = pool();
    const int64_t seated = held != nullptr ? slot() : -1;
    if (held == nullptr || held->state_binding_of(seated).is_null()) {
        return Dictionary();
    }
    return held->reachability_report_of(seated);
}

void NetwPredictionHandle::notify_contact() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->notify_contact();
    }
}

void NetwPredictionHandle::set_predict_commands(const Callable &p_predictor) {
    command_predictor = p_predictor;
}

void NetwPredictionHandle::restate_declaration() {
    reconfigure();
}

void NetwPredictionHandle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("bind_entity", "entity"),
        &NetwPredictionHandle::bind_entity
    );
    ClassDB::bind_method(D_METHOD("entity"), &NetwPredictionHandle::get_entity);
    ClassDB::bind_method(
        D_METHOD("is_registered"),
        &NetwPredictionHandle::is_registered
    );
    ClassDB::bind_method(
        D_METHOD("sensor", "name", "default"),
        &NetwPredictionHandle::sensor,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("resolved_recovery_policy"),
        &NetwPredictionHandle::resolved_recovery_policy
    );
    ClassDB::bind_method(
        D_METHOD("simulate_tick", "delta", "tick"),
        &NetwPredictionHandle::simulate_tick
    );
    ClassDB::bind_method(
        D_METHOD("simulate_frame", "delta"),
        &NetwPredictionHandle::simulate_frame
    );
    ClassDB::bind_method(
        D_METHOD("reachability"),
        &NetwPredictionHandle::reachability
    );
    ClassDB::bind_method(
        D_METHOD("notify_contact"),
        &NetwPredictionHandle::notify_contact
    );

    ClassDB::bind_method(
        D_METHOD("get_predict_commands"),
        &NetwPredictionHandle::get_predict_commands
    );
    ClassDB::bind_method(
        D_METHOD("set_predict_commands", "value"),
        &NetwPredictionHandle::set_predict_commands
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "predict_commands"),
        "set_predict_commands",
        "get_predict_commands"
    );
    ClassDB::bind_method(
        D_METHOD("get_recovery_policy"),
        &NetwPredictionHandle::get_recovery_policy
    );
    ClassDB::bind_method(
        D_METHOD("set_recovery_policy", "value"),
        &NetwPredictionHandle::set_recovery_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "recovery_policy",
            PROPERTY_HINT_ENUM,
            "Rebase Replay,Rebase Recover,Delay Closed,Observe,Auto",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.RecoveryPolicy"
        ),
        "set_recovery_policy",
        "get_recovery_policy"
    );
    ClassDB::bind_method(
        D_METHOD("get_breach_response"),
        &NetwPredictionHandle::get_breach_response
    );
    ClassDB::bind_method(
        D_METHOD("set_breach_response", "value"),
        &NetwPredictionHandle::set_breach_response
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "breach_response",
            PROPERTY_HINT_ENUM,
            "Discard,Fallback,Trace",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.BreachResponse"
        ),
        "set_breach_response",
        "get_breach_response"
    );
    ClassDB::bind_method(
        D_METHOD("get_sensors"),
        &NetwPredictionHandle::get_sensors
    );
    ClassDB::bind_method(
        D_METHOD("set_sensors", "value"),
        &NetwPredictionHandle::set_sensors
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "sensors"),
        "set_sensors",
        "get_sensors"
    );
    ClassDB::bind_method(
        D_METHOD("get_epoch"),
        &NetwPredictionHandle::get_epoch
    );
    ClassDB::bind_method(
        D_METHOD("set_epoch", "value"),
        &NetwPredictionHandle::set_epoch
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "epoch"), "set_epoch", "get_epoch");
    ClassDB::bind_method(
        D_METHOD("get_witness_contacts"),
        &NetwPredictionHandle::get_witness_contacts
    );
    ClassDB::bind_method(
        D_METHOD("set_witness_contacts", "value"),
        &NetwPredictionHandle::set_witness_contacts
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "witness_contacts"),
        "set_witness_contacts",
        "get_witness_contacts"
    );
    ClassDB::bind_method(
        D_METHOD("get_transport_corridor"),
        &NetwPredictionHandle::get_transport_corridor
    );
    ClassDB::bind_method(
        D_METHOD("set_transport_corridor", "value"),
        &NetwPredictionHandle::set_transport_corridor
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "transport_corridor"),
        "set_transport_corridor",
        "get_transport_corridor"
    );
    ClassDB::bind_method(
        D_METHOD("get_teleport_threshold"),
        &NetwPredictionHandle::get_teleport_threshold
    );
    ClassDB::bind_method(
        D_METHOD("set_teleport_threshold", "value"),
        &NetwPredictionHandle::set_teleport_threshold
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "teleport_threshold"),
        "set_teleport_threshold",
        "get_teleport_threshold"
    );
    ClassDB::bind_method(
        D_METHOD("get_collision_cooldown_ticks"),
        &NetwPredictionHandle::get_collision_cooldown_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_collision_cooldown_ticks", "value"),
        &NetwPredictionHandle::set_collision_cooldown_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "collision_cooldown_ticks"),
        "set_collision_cooldown_ticks",
        "get_collision_cooldown_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_sleeping"),
        &NetwPredictionHandle::get_sleeping
    );
    ClassDB::bind_method(
        D_METHOD("set_sleeping", "value"),
        &NetwPredictionHandle::set_sleeping
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "sleeping"),
        "set_sleeping",
        "get_sleeping"
    );
    ClassDB::bind_method(
        D_METHOD("get_missing_policy"),
        &NetwPredictionHandle::get_missing_policy
    );
    ClassDB::bind_method(
        D_METHOD("set_missing_policy", "value"),
        &NetwPredictionHandle::set_missing_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "missing_policy",
            PROPERTY_HINT_ENUM,
            "None,RepeatLast,Extrapolate,Custom",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.MissingInput"
        ),
        "set_missing_policy",
        "get_missing_policy"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_consume_per_tick"),
        &NetwPredictionHandle::get_max_consume_per_tick
    );
    ClassDB::bind_method(
        D_METHOD("set_max_consume_per_tick", "value"),
        &NetwPredictionHandle::set_max_consume_per_tick
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_consume_per_tick"),
        "set_max_consume_per_tick",
        "get_max_consume_per_tick"
    );
    ClassDB::bind_method(
        D_METHOD("get_consume_buffer_ticks"),
        &NetwPredictionHandle::get_consume_buffer_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_consume_buffer_ticks", "value"),
        &NetwPredictionHandle::set_consume_buffer_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "consume_buffer_ticks"),
        "set_consume_buffer_ticks",
        "get_consume_buffer_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_replay_buffer_depth"),
        &NetwPredictionHandle::get_replay_buffer_depth
    );
    ClassDB::bind_method(
        D_METHOD("set_replay_buffer_depth", "value"),
        &NetwPredictionHandle::set_replay_buffer_depth
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "replay_buffer_depth"),
        "set_replay_buffer_depth",
        "get_replay_buffer_depth"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_consume_lag_ticks"),
        &NetwPredictionHandle::get_max_consume_lag_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_max_consume_lag_ticks", "value"),
        &NetwPredictionHandle::set_max_consume_lag_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_consume_lag_ticks"),
        "set_max_consume_lag_ticks",
        "get_max_consume_lag_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_ack_age_ticks"),
        &NetwPredictionHandle::get_ack_age_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_ack_age_ticks", "value"),
        &NetwPredictionHandle::set_ack_age_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "ack_age_ticks"),
        "set_ack_age_ticks",
        "get_ack_age_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_divergence_epsilon"),
        &NetwPredictionHandle::get_divergence_epsilon
    );
    ClassDB::bind_method(
        D_METHOD("set_divergence_epsilon", "value"),
        &NetwPredictionHandle::set_divergence_epsilon
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "divergence_epsilon"),
        "set_divergence_epsilon",
        "get_divergence_epsilon"
    );
    ClassDB::bind_method(
        D_METHOD("get_reconcile_mode"),
        &NetwPredictionHandle::get_reconcile_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_reconcile_mode", "value"),
        &NetwPredictionHandle::set_reconcile_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "reconcile_mode",
            PROPERTY_HINT_ENUM,
            "Replay,State,Custom",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.Reconcile"
        ),
        "set_reconcile_mode",
        "get_reconcile_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_archetype"),
        &NetwPredictionHandle::get_archetype
    );
    ClassDB::bind_method(
        D_METHOD("set_archetype", "value"),
        &NetwPredictionHandle::set_archetype
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "archetype",
            PROPERTY_HINT_ENUM,
            "None,Scripted,SolverBody",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.Archetype"
        ),
        "set_archetype",
        "get_archetype"
    );

    ClassDB::bind_method(
        D_METHOD("get_stats"),
        &NetwPredictionHandle::get_stats
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "stats",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPredictStats",
            PROPERTY_USAGE_NONE
        ),
        godot::String(),
        "get_stats"
    );

    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName(),
        "GENERATOR_UNKNOWN_BEYOND_RETENTION",
        0
    );
    ADD_SIGNAL(MethodInfo(
        "divergence_detected",
        PropertyInfo(Variant::INT, "entry"),
        PropertyInfo(
            Variant::INT,
            "attribution",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.Attribution"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        "episode_opened",
        PropertyInfo(Variant::DICTIONARY, "report")
    ));
    ADD_SIGNAL(MethodInfo(
        "episode_closed",
        PropertyInfo(Variant::DICTIONARY, "report")
    ));
    ADD_SIGNAL(MethodInfo(
        "episode_fallback",
        PropertyInfo(Variant::DICTIONARY, "report")
    ));
    ADD_SIGNAL(MethodInfo(
        "recovered",
        PropertyInfo(Variant::INT, "entry"),
        PropertyInfo(Variant::DICTIONARY, "deltas"),
        PropertyInfo(Variant::BOOL, "teleported"),
        PropertyInfo(
            Variant::INT,
            "attribution",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPredict.Attribution"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        "state_evaluated",
        PropertyInfo(Variant::INT, "recv_tick"),
        PropertyInfo(Variant::INT, "ack"),
        PropertyInfo(Variant::FLOAT, "divergence"),
        PropertyInfo(Variant::BOOL, "diverged")
    ));
}

Ref<NetwPredictionHandle> build_prediction_handle(Object *p_entity) {
    Ref<NetwPredictionHandle> made;
    made.instantiate();
    made->bind_entity(Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entity)));
    return made;
}

} // namespace netw
