#include "netw/api/prediction.hpp"

#include <algorithm>
#include <cmath>

#include "godot/class_db.hpp"
#include "godot/math.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/game_call.hpp"
#include "netw/log.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"

using namespace godot;

namespace netw {

namespace {

const GameCall PREDICT_INPUT_CALL = {
    sys::PREDICTION,
    "NetwPrediction.predict_input",
    "func(entity: NetwEntity, tick: int) -> Dictionary",
    2
};

const GameCall WITNESS_CONTACTS_CALL = {
    sys::PREDICTION,
    "NetwPrediction.witness_contacts",
    "func() -> Dictionary",
    0
};

const GameCall CORRECTION_PATH_CALL = {
    sys::PREDICTION,
    "NetwPrediction.correction_path_clear",
    "func(current: Dictionary, proposed: Dictionary) -> bool",
    2
};

const GameCall SENSOR_CALL = {
    sys::PREDICTION,
    "A NetwPrediction.sensors entry",
    "func() -> Variant",
    0
};

} // namespace

const char *NetwPrediction::GENERATOR_UNKNOWN_BEYOND_RETENTION
    = "UNKNOWN_BEYOND_RETENTION";

NetwPrediction::~NetwPrediction() {
    if (counters.is_valid()) {
        counters->keep(facts);
    }
}

Ref<NetwPredictStats> NetwPrediction::get_stats() const {
    if (counters.is_null()) {
        counters.instantiate();
        counters->view(const_cast<NetwPredictStats::Book *>(&facts));
    }
    return counters;
}

NetwPredictTiming NetwPrediction::frame_reading() const {
    NetwPredictSlotEngine *held = engine();
    NetwMultiplayer *session = held == nullptr ? nullptr : held->core();
    if (session != nullptr) {
        return session->frame_timing();
    }
    return NetwPredictTiming::of(0, 0.0, 0.0);
}

NetwPredictSlotEngine *NetwPrediction::engine() const {
    return engine_seat;
}

NetwPredictionEngine *NetwPrediction::pool() const {
    NetwPredictSlotEngine *held = engine();
    return held == nullptr ? nullptr : held->seated_pool();
}

int64_t NetwPrediction::slot() const {
    NetwPredictSlotEngine *held = engine();
    return held == nullptr ? -1 : held->native_slot();
}

void NetwPrediction::reconfigure() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->reconfigure();
    }
}

void NetwPrediction::rewire() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->rewire();
    }
}

void NetwPrediction::bind_entity(const Ref<NetwEntity> &p_entity) {
    entity_id = p_entity.is_valid() ? p_entity->get_instance_id() : ObjectID();
}

void NetwPrediction::bind_engine(NetwPredictSlotEngine *p_engine) {
    engine_seat = p_engine;
}

void NetwPrediction::seat_control_changed(
    int64_t p_previous_peer,
    int64_t p_peer
) {
    if (engine_seat != nullptr) {
        engine_seat->on_control_changed(p_previous_peer, p_peer);
    }
}

void NetwPrediction::seat_arrival(
    predict::Listener p_listener,
    const predict::Arrival &p_arrival
) {
    if (engine_seat == nullptr) {
        return;
    }
    switch (p_listener) {
        case predict::Listener::STATE:
            engine_seat->on_state_frame(p_arrival);
            break;
        case predict::Listener::INPUT:
            engine_seat->on_input_frame(p_arrival);
            break;
        case predict::Listener::SIMULATED:
            engine_seat->on_simulated_state_frame(p_arrival);
            break;
        case predict::Listener::QUARANTINE:
            engine_seat->on_quarantine_state_frame(p_arrival);
            break;
        default:
            break;
    }
}

void NetwPrediction::seat_quarantine_witness(int64_t p_basis) {
    if (engine_seat != nullptr) {
        engine_seat->apply_quarantine_witness(p_basis);
    }
}

void NetwPrediction::seat_deferred_operator_retry(int64_t p_basis) {
    if (engine_seat != nullptr) {
        engine_seat->retry_deferred_operator(p_basis);
    }
}

void NetwPrediction::seat_fallback_at(int64_t p_transition, int p_attribution) {
    if (engine_seat != nullptr) {
        engine_seat->enter_fallback_at(p_transition, p_attribution);
    }
}

void NetwPrediction::seat_fallback(
    int64_t p_transition,
    int p_attribution,
    bool p_demoted
) {
    if (engine_seat != nullptr) {
        engine_seat->enter_fallback(p_transition, p_attribution, p_demoted);
    }
}

void NetwPrediction::seat_reachability_findings(const Array &p_findings) {
    if (engine_seat != nullptr) {
        engine_seat->emit_reachability_findings(p_findings);
    }
}

void NetwPrediction::seat_breach(
    int64_t p_transition,
    const Dictionary &p_solve
) {
    if (engine_seat != nullptr) {
        engine_seat->maybe_demote_for_breach(p_transition, p_solve);
    }
}

void NetwPrediction::seat_reconcile_mode() {
    if (engine_seat != nullptr) {
        engine_seat->admit_reconcile_mode();
    }
}

void NetwPrediction::seat_command_frame() {
    if (engine_seat != nullptr) {
        engine_seat->send_command_frame();
    }
}

Ref<NetwPredictRecovery> NetwPrediction::seat_recover_seam(
    const Dictionary &p_carried,
    const Dictionary &p_predicted,
    NetwPrediction::RecoveryPolicy p_policy,
    NetwSimulation::Restore p_snap_restore,
    const Dictionary &p_projection,
    const Dictionary &p_before,
    const Dictionary &p_tier_errors,
    const Dictionary &p_context,
    double p_tick_delta
) {
    return engine_seat == nullptr ? Ref<NetwPredictRecovery>()
                                  : engine_seat->recover_through_seam(
                                        p_carried,
                                        p_predicted,
                                        p_policy,
                                        p_snap_restore,
                                        p_projection,
                                        p_before,
                                        p_tier_errors,
                                        p_context,
                                        p_tick_delta
                                    );
}

Ref<NetwPredictJudgement> NetwPrediction::seat_evaluate_seam(
    NetwPrediction::Domain p_domain,
    NetwPrediction::ExactVerdict p_exact_verdict,
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

Ref<NetwEntity> NetwPrediction::get_entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(ObjectDB::get_instance(entity_id))
    );
}

bool NetwPrediction::is_registered() const {
    return engine() != nullptr;
}

Ref<NetwSimulation> NetwPrediction::simulation() const {
    const Ref<NetwEntity> bound = get_entity();
    return bound.is_valid() ? bound->get_simulation() : Ref<NetwSimulation>();
}

void NetwPrediction::step_changed() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->push_simulate();
    }
}

Callable NetwPrediction::get_simulate() const {
    const Ref<NetwSimulation> held = simulation();
    return held.is_valid() ? held->get_step() : Callable();
}

NetwSimulation::Schedule NetwPrediction::get_schedule() const {
    const Ref<NetwSimulation> held = simulation();
    return held.is_valid() ? held->resolved_schedule()
                           : NetwSimulation::SCHEDULE_TICK;
}

NetwSimulation::Restore NetwPrediction::get_snap_restore() const {
    const Ref<NetwSimulation> held = simulation();
    return held.is_valid()
            && held->get_restore() == NetwSimulation::RESTORE_EXTRAPOLATED
        ? NetwSimulation::RESTORE_EXTRAPOLATED
        : NetwSimulation::RESTORE_EXACT;
}

int NetwPrediction::get_max_restore_ticks() const {
    const Ref<NetwSimulation> held = simulation();
    return held.is_valid() ? held->get_max_restore_ticks() : 6;
}

void NetwPrediction::set_input_source(NetwPrediction::InputSource p_value) {
    input_source_value = p_value;
}

void NetwPrediction::set_sim_mode(NetwPrediction::SimMode p_value) {
    sim_mode_value = p_value;
}

void NetwPrediction::set_recovery_policy(
    NetwPrediction::RecoveryPolicy p_value
) {
    recovery_policy_value = p_value;
    reconfigure();
    rewire();
}

void NetwPrediction::set_breach_response(
    NetwPrediction::BreachResponse p_value
) {
    breach_response_value = p_value;
    NetwPredictionEngine *held = pool();
    if (held != nullptr) {
        held->note_breach_source(slot(), StringName("code"));
    }
}

void NetwPrediction::set_sensors(const Dictionary &p_value) {
    const Array sensors = p_value.values();
    for (int64_t at = 0; at < sensors.size(); ++at) {
        if (sensors[at].get_type() == Variant::CALLABLE) {
            game_call_declared(Callable(sensors[at]), SENSOR_CALL);
        }
    }
    sensor_table = p_value;
}

void NetwPrediction::set_epoch(int64_t p_value) {
    epoch_value = p_value;
    NetwPredictionEngine *held = pool();
    if (held != nullptr) {
        held->set_declared_epoch(slot(), p_value);
    }
}

void NetwPrediction::set_witness_contacts(const Callable &p_value) {
    NETW_ERR_COND(
        !p_value.is_null() && !p_value.is_valid(),
        sys::PREDICTION,
        "NetwPrediction.witness_contacts needs a valid Callable, so "
        "the value was ignored. Pass a method of a live object."
    );
    game_call_declared(p_value, WITNESS_CONTACTS_CALL);
    witness_sampler = p_value;
    rewire();
}

void NetwPrediction::set_correction_path_clear(const Callable &p_value) {
    NETW_ERR_COND(
        !p_value.is_null() && !p_value.is_valid(),
        sys::PREDICTION,
        "NetwPrediction.correction_path_clear needs a valid Callable, "
        "so the value was ignored. Pass a method of a live object."
    );
    game_call_declared(p_value, CORRECTION_PATH_CALL);
    corridor_sweep = p_value;
}

void NetwPrediction::set_teleport_threshold(double p_value) {
    teleport_value = std::max(0.0, p_value);
    reconfigure();
}

void NetwPrediction::set_collision_cooldown_ticks(int p_value) {
    cooldown_value = std::max(0, p_value);
    reconfigure();
}

void NetwPrediction::set_sleeping(bool p_value) {
    sleeping_value = p_value;
}

void NetwPrediction::set_missing_policy(NetwPrediction::MissingInput p_value) {
    missing_policy_value = p_value;
}

void NetwPrediction::set_max_consume_per_tick(int p_value) {
    max_consume_per_tick_value = p_value;
}

void NetwPrediction::set_consume_buffer_ticks(int p_value) {
    consume_buffer_value = p_value;
}

void NetwPrediction::set_replay_buffer_depth(int p_value) {
    replay_buffer_value = std::max(0, p_value);
}

void NetwPrediction::set_max_consume_lag_ticks(int p_value) {
    max_consume_lag_value = std::max(0, p_value);
}

void NetwPrediction::set_ack_age_ticks(int p_value) {
    ack_age_value = p_value;
}

void NetwPrediction::set_divergence_epsilon(double p_value) {
    epsilon_value = std::max(0.0, p_value);
    reconfigure();
}

void NetwPrediction::set_reconcile_mode(NetwPrediction::Reconcile p_value) {
    reconcile_value = p_value;
    reconfigure();
}

void NetwPrediction::set_archetype(NetwPrediction::Archetype p_value) {
    archetype_value = p_value;
    const Dictionary axes = NetwPredictionEngine::archetype_axes(p_value);
    if (!bool(axes[StringName("declared")])) {
        return;
    }
    const Ref<NetwSimulation> held = simulation();
    if (held.is_valid()) {
        held->preset(
            NetwSimulation::Schedule(int(axes[StringName("schedule")])),
            bool(axes[StringName("declares_snap_restore")]),
            NetwSimulation::Restore(int(axes[StringName("snap_restore")]))
        );
    }
    set_missing_policy(
        static_cast<NetwPrediction::MissingInput>(
            int(axes[StringName("missing_policy")])
        )
    );
    set_recovery_policy(
        static_cast<NetwPrediction::RecoveryPolicy>(
            int(axes[StringName("recovery_policy")])
        )
    );
    if (bool(axes[StringName("declares_teleport_threshold")])) {
        set_teleport_threshold(double(axes[StringName("teleport_threshold")]));
    }
}

Variant NetwPrediction::sensor(
    const StringName &p_name,
    const Variant &p_default
) const {
    NetwPredictionEngine *held = pool();
    if (held == nullptr) {
        return p_default;
    }
    return held->sensor_samples(slot()).get(p_name, p_default);
}

NetwPrediction::RecoveryPolicy NetwPrediction::
    resolved_recovery_policy() const {
    const bool solves = body_solves();
    const int schedule = get_schedule();
    return static_cast<NetwPrediction::RecoveryPolicy>(
        predict::integrable_recovery_policy(
            predict::resolve_recovery_policy(
                recovery_policy_value,
                solves,
                schedule
            ),
            schedule,
            solves
        )
    );
}

int NetwPrediction::resolved_correction() const {
    return predict::correction_for_recovery_policy(resolved_recovery_policy());
}

bool NetwPrediction::body_solves() const {
    NetwPredictionEngine *held = pool();
    if (held != nullptr && held->owner_bound(slot())) {
        return held->owner_solves(slot());
    }
    const Ref<NetwEntity> bound = get_entity();
    Object *body = bound.is_valid() ? bound->get_owner() : nullptr;
    return Object::cast_to<RigidBody2D>(body) != nullptr
        || Object::cast_to<RigidBody3D>(body) != nullptr;
}

void NetwPrediction::simulate_tick(double p_delta, int64_t p_tick) {
    NetwPredictSlotEngine *held = engine();
    if (held == nullptr) {
        return;
    }
    const NetwPredictTiming reading = frame_reading();
    held->simulate_tick(
        NetwPredictTiming::of(p_tick, p_delta, reading.get_ticktime())
    );
}

void NetwPrediction::simulate_frame(double p_delta) {
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

Dictionary NetwPrediction::reachability() const {
    NetwPredictionEngine *held = pool();
    const int64_t seated = held != nullptr ? slot() : -1;
    if (held == nullptr || held->state_binding_of(seated) == nullptr) {
        return Dictionary();
    }
    return held->reachability_report_of(seated);
}

void NetwPrediction::notify_contact() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->notify_contact();
    }
}

void NetwPrediction::set_predict_input(const Callable &p_predictor) {
    game_call_declared(p_predictor, PREDICT_INPUT_CALL);
    command_predictor = p_predictor;
}

void NetwPrediction::restate_declaration() {
    reconfigure();
}

void NetwPrediction::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("bind_entity", "entity"),
        &NetwPrediction::bind_entity
    );
    ClassDB::bind_method(D_METHOD("entity"), &NetwPrediction::get_entity);
    ClassDB::bind_method(
        D_METHOD("is_registered"),
        &NetwPrediction::is_registered
    );
    ClassDB::bind_method(
        D_METHOD("sensor", "name", "default"),
        &NetwPrediction::sensor,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("resolved_recovery_policy"),
        &NetwPrediction::resolved_recovery_policy
    );
    ClassDB::bind_method(
        D_METHOD("simulate_tick", "delta", "tick"),
        &NetwPrediction::simulate_tick
    );
    ClassDB::bind_method(
        D_METHOD("simulate_frame", "delta"),
        &NetwPrediction::simulate_frame
    );
    ClassDB::bind_method(
        D_METHOD("reachability"),
        &NetwPrediction::reachability
    );
    ClassDB::bind_method(
        D_METHOD("notify_contact"),
        &NetwPrediction::notify_contact
    );

    ClassDB::bind_method(
        D_METHOD("get_predict_input"),
        &NetwPrediction::get_predict_input
    );
    ClassDB::bind_method(
        D_METHOD("set_predict_input", "value"),
        &NetwPrediction::set_predict_input
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "predict_input"),
        "set_predict_input",
        "get_predict_input"
    );
    ClassDB::bind_method(
        D_METHOD("get_recovery_policy"),
        &NetwPrediction::get_recovery_policy
    );
    ClassDB::bind_method(
        D_METHOD("set_recovery_policy", "value"),
        &NetwPrediction::set_recovery_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "recovery_policy",
            PROPERTY_HINT_ENUM,
            "Rebase Replay,Rebase Recover,Delay Closed,Observe,Auto",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.RecoveryPolicy"
        ),
        "set_recovery_policy",
        "get_recovery_policy"
    );
    ClassDB::bind_method(
        D_METHOD("get_breach_response"),
        &NetwPrediction::get_breach_response
    );
    ClassDB::bind_method(
        D_METHOD("set_breach_response", "value"),
        &NetwPrediction::set_breach_response
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "breach_response",
            PROPERTY_HINT_ENUM,
            "Discard,Fallback,Trace",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.BreachResponse"
        ),
        "set_breach_response",
        "get_breach_response"
    );
    ClassDB::bind_method(D_METHOD("get_sensors"), &NetwPrediction::get_sensors);
    ClassDB::bind_method(
        D_METHOD("set_sensors", "value"),
        &NetwPrediction::set_sensors
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "sensors"),
        "set_sensors",
        "get_sensors"
    );
    ClassDB::bind_method(D_METHOD("get_epoch"), &NetwPrediction::get_epoch);
    ClassDB::bind_method(
        D_METHOD("set_epoch", "value"),
        &NetwPrediction::set_epoch
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "epoch"), "set_epoch", "get_epoch");
    ClassDB::bind_method(
        D_METHOD("get_witness_contacts"),
        &NetwPrediction::get_witness_contacts
    );
    ClassDB::bind_method(
        D_METHOD("set_witness_contacts", "value"),
        &NetwPrediction::set_witness_contacts
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "witness_contacts"),
        "set_witness_contacts",
        "get_witness_contacts"
    );
    ClassDB::bind_method(
        D_METHOD("get_correction_path_clear"),
        &NetwPrediction::get_correction_path_clear
    );
    ClassDB::bind_method(
        D_METHOD("set_correction_path_clear", "value"),
        &NetwPrediction::set_correction_path_clear
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "correction_path_clear"),
        "set_correction_path_clear",
        "get_correction_path_clear"
    );
    ClassDB::bind_method(
        D_METHOD("get_teleport_threshold"),
        &NetwPrediction::get_teleport_threshold
    );
    ClassDB::bind_method(
        D_METHOD("set_teleport_threshold", "value"),
        &NetwPrediction::set_teleport_threshold
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "teleport_threshold"),
        "set_teleport_threshold",
        "get_teleport_threshold"
    );
    ClassDB::bind_method(
        D_METHOD("get_collision_cooldown_ticks"),
        &NetwPrediction::get_collision_cooldown_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_collision_cooldown_ticks", "value"),
        &NetwPrediction::set_collision_cooldown_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "collision_cooldown_ticks"),
        "set_collision_cooldown_ticks",
        "get_collision_cooldown_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_sleeping"),
        &NetwPrediction::get_sleeping
    );
    ClassDB::bind_method(
        D_METHOD("set_sleeping", "value"),
        &NetwPrediction::set_sleeping
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "sleeping"),
        "set_sleeping",
        "get_sleeping"
    );
    ClassDB::bind_method(
        D_METHOD("get_missing_policy"),
        &NetwPrediction::get_missing_policy
    );
    ClassDB::bind_method(
        D_METHOD("set_missing_policy", "value"),
        &NetwPrediction::set_missing_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "missing_policy",
            PROPERTY_HINT_ENUM,
            "None,RepeatLast,Extrapolate,Custom",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.MissingInput"
        ),
        "set_missing_policy",
        "get_missing_policy"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_consume_per_tick"),
        &NetwPrediction::get_max_consume_per_tick
    );
    ClassDB::bind_method(
        D_METHOD("set_max_consume_per_tick", "value"),
        &NetwPrediction::set_max_consume_per_tick
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_consume_per_tick"),
        "set_max_consume_per_tick",
        "get_max_consume_per_tick"
    );
    ClassDB::bind_method(
        D_METHOD("get_consume_buffer_ticks"),
        &NetwPrediction::get_consume_buffer_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_consume_buffer_ticks", "value"),
        &NetwPrediction::set_consume_buffer_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "consume_buffer_ticks"),
        "set_consume_buffer_ticks",
        "get_consume_buffer_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_replay_buffer_depth"),
        &NetwPrediction::get_replay_buffer_depth
    );
    ClassDB::bind_method(
        D_METHOD("set_replay_buffer_depth", "value"),
        &NetwPrediction::set_replay_buffer_depth
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "replay_buffer_depth"),
        "set_replay_buffer_depth",
        "get_replay_buffer_depth"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_consume_lag_ticks"),
        &NetwPrediction::get_max_consume_lag_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_max_consume_lag_ticks", "value"),
        &NetwPrediction::set_max_consume_lag_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_consume_lag_ticks"),
        "set_max_consume_lag_ticks",
        "get_max_consume_lag_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_ack_age_ticks"),
        &NetwPrediction::get_ack_age_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_ack_age_ticks", "value"),
        &NetwPrediction::set_ack_age_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "ack_age_ticks"),
        "set_ack_age_ticks",
        "get_ack_age_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_divergence_epsilon"),
        &NetwPrediction::get_divergence_epsilon
    );
    ClassDB::bind_method(
        D_METHOD("set_divergence_epsilon", "value"),
        &NetwPrediction::set_divergence_epsilon
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "divergence_epsilon"),
        "set_divergence_epsilon",
        "get_divergence_epsilon"
    );
    ClassDB::bind_method(
        D_METHOD("get_reconcile_mode"),
        &NetwPrediction::get_reconcile_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_reconcile_mode", "value"),
        &NetwPrediction::set_reconcile_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "reconcile_mode",
            PROPERTY_HINT_ENUM,
            "Replay,State,Custom",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.Reconcile"
        ),
        "set_reconcile_mode",
        "get_reconcile_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_archetype"),
        &NetwPrediction::get_archetype
    );
    ClassDB::bind_method(
        D_METHOD("set_archetype", "value"),
        &NetwPrediction::set_archetype
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "archetype",
            PROPERTY_HINT_ENUM,
            "None,Scripted,SolverBody",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.Archetype"
        ),
        "set_archetype",
        "get_archetype"
    );

    ClassDB::bind_method(D_METHOD("get_stats"), &NetwPrediction::get_stats);
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

    BIND_CONSTANT(ACK_AGE_MAX);

    BIND_ENUM_CONSTANT(CONTACT_CLASS_NONE);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_DECLARED_SUPPORT);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_OTHER_STATIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_PREDICTED_DYNAMIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_UNPREDICTED_DYNAMIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_KINEMATIC_PROXY);

    BIND_ENUM_CONSTANT(COMMAND_ORIGIN_PREDICTED);
    BIND_ENUM_CONSTANT(COMMAND_ORIGIN_RELAYED);

    BIND_ENUM_CONSTANT(DRIVE_KIND_NONE);
    BIND_ENUM_CONSTANT(DRIVE_KIND_FRESH);
    BIND_ENUM_CONSTANT(DRIVE_KIND_REPEAT);
    BIND_ENUM_CONSTANT(DRIVE_KIND_HOLD);
    BIND_ENUM_CONSTANT(DRIVE_KIND_STARVED);
    BIND_ENUM_CONSTANT(DRIVE_KIND_FOLD_DRIVE);
    BIND_ENUM_CONSTANT(DRIVE_KIND_MISSING);
    BIND_ENUM_CONSTANT(DRIVE_KIND_SUBSTITUTED);

    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_NONE);
    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_MIXED);
    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_ALL_WITHHELD);

    BIND_ENUM_CONSTANT(CONSUME_ACTION_REPLAY);
    BIND_ENUM_CONSTANT(CONSUME_ACTION_HOLD);
    BIND_ENUM_CONSTANT(CONSUME_ACTION_STARVED);

    BIND_ENUM_CONSTANT(EXACT_VERDICT_UNJUDGED);
    BIND_ENUM_CONSTANT(EXACT_VERDICT_EQUAL);
    BIND_ENUM_CONSTANT(EXACT_VERDICT_UNEQUAL);

    BIND_ENUM_CONSTANT(MISSING_INPUT_STALL);
    BIND_ENUM_CONSTANT(MISSING_INPUT_REPEAT_LAST);

    BIND_ENUM_CONSTANT(ARCHETYPE_NONE);
    BIND_ENUM_CONSTANT(ARCHETYPE_SCRIPTED);
    BIND_ENUM_CONSTANT(ARCHETYPE_SOLVER_BODY);

    BIND_ENUM_CONSTANT(RECOVERY_POLICY_REBASE_REPLAY);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_REBASE_RECOVER);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_DELAY_CLOSED);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_OBSERVE);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_AUTO);

    BIND_ENUM_CONSTANT(RECONCILE_INDEPENDENT);
    BIND_ENUM_CONSTANT(RECONCILE_JOINT);

    BIND_ENUM_CONSTANT(BREACH_RESPONSE_PREDICT_THROUGH);
    BIND_ENUM_CONSTANT(BREACH_RESPONSE_DEMOTE);

    BIND_ENUM_CONSTANT(EPISODE_STATE_OPEN);
    BIND_ENUM_CONSTANT(EPISODE_STATE_CLOSED);
    BIND_ENUM_CONSTANT(EPISODE_STATE_FALLBACK);
    BIND_ENUM_CONSTANT(EPISODE_CHANGE_OPENED);
    BIND_ENUM_CONSTANT(EPISODE_CHANGE_FALLBACK);
    BIND_ENUM_CONSTANT(EPISODE_CHANGE_CLOSED);

    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_PENDING);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_CONTRACTED);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_FAILED_TO_CONTRACT);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_INTRODUCED_BOUNDARY);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_WITHHELD);

    BIND_ENUM_CONSTANT(ATTRIBUTION_UNKNOWN);
    BIND_ENUM_CONSTANT(ATTRIBUTION_PRE_STATE);
    BIND_ENUM_CONSTANT(ATTRIBUTION_COMMAND);
    BIND_ENUM_CONSTANT(ATTRIBUTION_ENVIRONMENT);
    BIND_ENUM_CONSTANT(ATTRIBUTION_TOPOLOGY);
    BIND_ENUM_CONSTANT(ATTRIBUTION_EXECUTION);
    BIND_ENUM_CONSTANT(ATTRIBUTION_CONTACT);
    BIND_ENUM_CONSTANT(ATTRIBUTION_CLOSURE);

    BIND_ENUM_CONSTANT(OPERATOR_NONE);
    BIND_ENUM_CONSTANT(OPERATOR_REBASE_PROJECTED);
    BIND_ENUM_CONSTANT(OPERATOR_REBASE_EXACT);
    BIND_ENUM_CONSTANT(OPERATOR_FULL_CLOSURE);
    BIND_ENUM_CONSTANT(OPERATOR_TRANSPORT_DELTA);
    BIND_ENUM_CONSTANT(OPERATOR_RESEED);
    BIND_ENUM_CONSTANT(OPERATOR_RESEED_ALIGN);
    BIND_ENUM_CONSTANT(OPERATOR_DEMOTE);
    BIND_ENUM_CONSTANT(OPERATOR_DISSIPATE);
    BIND_ENUM_CONSTANT(OPERATOR_JOINT_REBASE);

    BIND_ENUM_CONSTANT(DOMAIN_IN);
    BIND_ENUM_CONSTANT(DOMAIN_OUT);

    ADD_SIGNAL(MethodInfo(
        "divergence_detected",
        PropertyInfo(Variant::INT, "entry"),
        PropertyInfo(
            Variant::INT,
            "attribution",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.Attribution"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        "episode_changed",
        PropertyInfo(
            Variant::INT,
            "change",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPrediction.EpisodeChange"
        ),
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
            "NetwPrediction.Attribution"
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

Ref<NetwPrediction> build_prediction_handle(Object *p_entity) {
    Ref<NetwPrediction> made;
    made.instantiate();
    made->bind_entity(Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entity)));
    return made;
}

} // namespace netw
