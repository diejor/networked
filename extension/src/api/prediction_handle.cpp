#include "netw/api/prediction_handle.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "godot/class_db.hpp"
#include "godot/math.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_field_recovery.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/log.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr double UNBOUNDED = std::numeric_limits<double>::infinity();

} // namespace

const char *NetwPredictionHandle::GENERATOR_UNKNOWN_BEYOND_RETENTION
    = "UNKNOWN_BEYOND_RETENTION";

NetwPredictionHandle::NetwPredictionHandle() {
    island_rule.instantiate();
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
    island_rule->bind_owner(p_entity);
    island_rule->watch_declaration(
        callable_mp(this, &NetwPredictionHandle::restate_declaration)
    );
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
    NetwPredict::CorrectionMode p_correction,
    NetwPredict::RestoreMode p_snap_restore,
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
                                        p_correction,
                                        p_snap_restore,
                                        p_projection,
                                        p_before,
                                        p_tier_errors,
                                        p_context,
                                        p_tick_delta
                                    );
}

Ref<NetwPredictJudgement> NetwPredictionHandle::seat_evaluate_seam(
    NetwPredictJournal::Domain p_domain,
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

void NetwPredictionHandle::set_simulate(const Callable &p_value) {
    simulate_step = p_value;
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->push_simulate();
    }
}

void NetwPredictionHandle::set_schedule(NetwPredict::Schedule p_value) {
    schedule_value = p_value;
    reconfigure();
}

void NetwPredictionHandle::set_correction_mode(int p_value) {
    correction_value = p_value;
    reconfigure();
}

void NetwPredictionHandle::set_snap_restore(int p_value) {
    restore_value = p_value;
    reconfigure();
}

void NetwPredictionHandle::set_input_source(int p_value) {
    input_source_value = p_value;
}

void NetwPredictionHandle::set_sim_mode(int p_value) {
    sim_mode_value = p_value;
}

void NetwPredictionHandle::set_recovery_policy(int p_value) {
    recovery_policy_value = p_value;
    set_correction_mode(
        NetwPredictionEngine::correction_for_recovery_policy(p_value)
    );
    rewire();
}

void NetwPredictionHandle::set_breach_response(int p_value) {
    breach_response_value = p_value;
    NetwPredictionEngine *held = pool();
    if (held != nullptr) {
        held->note_breach_source(slot(), StringName("code"));
    }
}

void NetwPredictionHandle::set_island(const Ref<NetwPredictIsland> &p_value) {
    island_rule = p_value;
    if (island_rule.is_null()) {
        island_rule.instantiate();
    }
    island_rule->bind_owner(get_entity());
    island_rule->watch_declaration(
        callable_mp(this, &NetwPredictionHandle::restate_declaration)
    );
    reconfigure();
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

void NetwPredictionHandle::set_max_restore_ticks(int p_value) {
    max_restore_value = p_value;
    reconfigure();
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

void NetwPredictionHandle::set_missing_policy(int p_value) {
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

void NetwPredictionHandle::set_reconcile_mode(int p_value) {
    reconcile_value = p_value;
    reconfigure();
}

void NetwPredictionHandle::set_archetype(NetwPredict::Archetype p_value) {
    archetype_value = p_value;
    const Dictionary axes = NetwPredictionEngine::archetype_axes(p_value);
    if (!bool(axes[StringName("declared")])) {
        return;
    }
    set_schedule(
        static_cast<NetwPredict::Schedule>(int(axes[StringName("schedule")]))
    );
    set_missing_policy(int(axes[StringName("missing_policy")]));
    set_recovery_policy(int(axes[StringName("recovery_policy")]));
    if (bool(axes[StringName("declares_snap_restore")])) {
        set_snap_restore(int(axes[StringName("snap_restore")]));
    }
    if (bool(axes[StringName("declares_teleport_threshold")])) {
        set_teleport_threshold(double(axes[StringName("teleport_threshold")]));
    }
}

Dictionary NetwPredictionHandle::get_last_field_divergence() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? Dictionary() : held->divergence_report(slot());
}

Dictionary NetwPredictionHandle::get_field_recovery() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? Dictionary() : held->field_recovery(slot());
}

int NetwPredictionHandle::get_last_compare_staleness() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? -1 : held->compare_staleness_of(slot());
}

Dictionary NetwPredictionHandle::get_last_tier_errors() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? Dictionary() : held->tier_error_report(slot());
}

int NetwPredictionHandle::get_last_verdict_reason() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? NetwPredict::VERDICT_REASON_NONE
                           : held->verdict_reason_of(slot());
}

bool NetwPredictionHandle::get_is_reconciling() const {
    NetwPredictionEngine *held = pool();
    return held != nullptr && held->reconciling(slot());
}

int64_t NetwPredictionHandle::get_acknowledged_tick() const {
    return int64_t(counters->get(StringName("ack_confirmed")));
}

int NetwPredictionHandle::get_last_attribution() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? int(Attribution::UNKNOWN)
                           : held->attribution_of(slot());
}

int64_t NetwPredictionHandle::get_last_attributed_transition() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? -1 : held->attributed_transition_of(slot());
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
    if (recovery_policy_value >= 0) {
        return static_cast<NetwPredict::RecoveryPolicy>(recovery_policy_value);
    }
    return resolved_correction_mode() == NetwPredict::CORRECTION_MODE_REPLAY
        ? NetwPredict::RECOVERY_POLICY_REBASE_REPLAY
        : NetwPredict::RECOVERY_POLICY_REBASE_RECOVER;
}

NetwPredict::CorrectionMode NetwPredictionHandle::
    resolved_correction_mode() const {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        return static_cast<NetwPredict::CorrectionMode>(
            held->resolved_correction_mode()
        );
    }
    const Ref<NetwEntity> bound = get_entity();
    Object *body = bound.is_valid() ? bound->get_owner() : nullptr;
    return resolve_correction_mode_for(
        body,
        static_cast<NetwPredict::CorrectionMode>(correction_value)
    );
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

Ref<NetwPredictJournal> NetwPredictionHandle::journal() const {
    NetwPredictionEngine *held = pool();
    if (held == nullptr) {
        Ref<NetwPredictJournal> empty;
        empty.instantiate();
        return empty;
    }
    return held->journal_snapshot(slot());
}

Dictionary NetwPredictionHandle::teleport_distances() const {
    NetwPredictionEngine *held = pool();
    return held != nullptr ? held->teleport_distances(slot()) : Dictionary();
}

Dictionary NetwPredictionHandle::reachability() const {
    NetwPredictionEngine *held = pool();
    const int64_t seated = held != nullptr ? slot() : -1;
    if (held == nullptr || held->state_binding_of(seated).is_null()) {
        return Dictionary();
    }
    return held->reachability_report_of(seated);
}

Dictionary NetwPredictionHandle::episode() const {
    NetwPredictionEngine *held = pool();
    return held != nullptr ? held->episode_record(slot()) : Dictionary();
}

Dictionary NetwPredictionHandle::episode_digest() const {
    NetwPredictionEngine *held = pool();
    const int64_t seated = held != nullptr ? slot() : -1;
    return seated >= 0 ? held->episode_digest(seated) : Dictionary();
}

TypedArray<Dictionary> NetwPredictionHandle::tape_transitions() const {
    NetwPredictionEngine *held = pool();
    return held != nullptr ? held->tape_transitions(slot())
                           : TypedArray<Dictionary>();
}

Dictionary NetwPredictionHandle::transition_state_at(
    int64_t p_transition
) const {
    NetwPredictSlotEngine *held = engine();
    if (held == nullptr) {
        return Dictionary();
    }
    return held->transition_state_at(p_transition);
}

void NetwPredictionHandle::record_server_input(
    int64_t p_tick,
    const Dictionary &p_input
) {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->record_server_input(p_tick, p_input);
    }
}

bool NetwPredictionHandle::has_consumed_state_tick(int64_t p_state_tick) const {
    NetwPredictSlotEngine *held = engine();
    return held == nullptr || held->has_consumed_state_tick(p_state_tick);
}

void NetwPredictionHandle::notify_contact() {
    NetwPredictSlotEngine *held = engine();
    if (held != nullptr) {
        held->notify_contact();
    }
}

int64_t NetwPredictionHandle::history_record_tick(int64_t p_fallback) const {
    NetwPredictSlotEngine *held = engine();
    if (held == nullptr) {
        return p_fallback;
    }
    return held->history_record_tick(p_fallback);
}

void NetwPredictionHandle::stamp_episode() {
    NetwPredictionEngine *held = pool();
    const int64_t seated = slot();
    if (held != nullptr && seated >= 0) {
        held->stamp_episode_revision(seated);
    }
}

void NetwPredictionHandle::set_simulated_by(
    const Ref<NetwEntity> &p_subject,
    bool p_enabled,
    const Callable &p_predictor
) {
    NetwPredictionEngine *held = pool();
    if (p_subject.is_null() || held == nullptr) {
        return;
    }
    const int64_t row = slot();
    const int64_t subject = p_subject->get_instance_id();
    bool changed = false;
    if (p_enabled) {
        changed = held->note_simulated_by(row, subject, p_predictor);
    } else {
        changed = held->clear_simulated_by(row, subject);
        if (held->simulation_subject_count(row) == 0
            && reconcile_value != NetwPredict::RECONCILE_INDEPENDENT) {
            reconcile_value = NetwPredict::RECONCILE_INDEPENDENT;
            NetwPredictSlotEngine *shell = engine();
            if (shell != nullptr) {
                shell->follow_relay_subscription(reconcile_value);
            }
        }
    }
    if (changed) {
        rewire();
    }
}

int NetwPredictionHandle::simulated_by_count() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? 0 : held->simulation_subject_count(slot());
}

Callable NetwPredictionHandle::predicted_command_callable() const {
    NetwPredictionEngine *held = pool();
    return held == nullptr ? Callable()
                           : held->first_simulation_predictor(slot());
}

void NetwPredictionHandle::restate_declaration() {
    reconfigure();
}

NetwPredict::Role NetwPredictionHandle::role_for_axes(
    NetwPredict::InputSource p_source,
    NetwPredict::SimMode p_mode
) {
    return static_cast<NetwPredict::Role>(
        NetwPredictionEngine::role_for_axes(int(p_source), int(p_mode))
    );
}

NetwPredict::CorrectionMode NetwPredictionHandle::resolve_correction_mode_for(
    Object *p_body,
    NetwPredict::CorrectionMode p_mode,
    bool p_solves
) {
    if (p_mode != NetwPredict::CORRECTION_MODE_AUTO) {
        return p_mode;
    }
    const bool rigid = Object::cast_to<RigidBody2D>(p_body) != nullptr
        || Object::cast_to<RigidBody3D>(p_body) != nullptr;
    return p_solves || rigid ? NetwPredict::CORRECTION_MODE_SNAP
                             : NetwPredict::CORRECTION_MODE_REPLAY;
}

double NetwPredictionHandle::value_error(
    const Variant &p_a,
    const Variant &p_b
) {
    return predict::value_error(p_a, p_b, false);
}

double NetwPredictionHandle::field_error(
    const Variant &p_a,
    const Variant &p_b,
    bool p_is_angle
) {
    return predict::value_error(p_a, p_b, p_is_angle);
}

double NetwPredictionHandle::divergence(
    const Dictionary &p_predicted,
    const Dictionary &p_authoritative,
    const Dictionary &p_angles
) {
    double worst = 0.0;
    const Array keys = p_authoritative.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const Variant key = keys[at];
        if (!p_predicted.has(key)) {
            return UNBOUNDED;
        }
        worst = std::max(
            worst,
            field_error(
                p_predicted[key],
                p_authoritative[key],
                p_angles.has(key)
            )
        );
    }
    return worst;
}

double NetwPredictionHandle::divergence_by_field(
    const Dictionary &p_predicted,
    const Dictionary &p_authoritative,
    Dictionary p_out,
    const Dictionary &p_angles
) {
    p_out.clear();
    double worst = 0.0;
    const Array keys = p_authoritative.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const Variant key = keys[at];
        double error = UNBOUNDED;
        if (p_predicted.has(key)) {
            error = field_error(
                p_predicted[key],
                p_authoritative[key],
                p_angles.has(key)
            );
        }
        p_out[key] = error;
        worst = std::max(worst, error);
    }
    return worst;
}

bool NetwPredictionHandle::triggers(double p_error, double p_tolerance) {
    return p_error > p_tolerance;
}

bool NetwPredictionHandle::diverged(
    const Dictionary &p_predicted,
    const Dictionary &p_authoritative,
    double p_epsilon,
    const Dictionary &p_overrides,
    const Dictionary &p_excludes,
    const Dictionary &p_angles
) {
    const Array keys = p_authoritative.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const Variant key = keys[at];
        if (p_excludes.has(key)) {
            continue;
        }
        if (!p_predicted.has(key)) {
            return true;
        }
        const double error = field_error(
            p_predicted[key],
            p_authoritative[key],
            p_angles.has(key)
        );
        if (triggers(error, double(p_overrides.get(key, p_epsilon)))) {
            return true;
        }
    }
    return false;
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
        D_METHOD("resolved_correction_mode"),
        &NetwPredictionHandle::resolved_correction_mode
    );
    ClassDB::bind_method(
        D_METHOD("simulate_tick", "delta", "tick"),
        &NetwPredictionHandle::simulate_tick
    );
    ClassDB::bind_method(
        D_METHOD("simulate_frame", "delta"),
        &NetwPredictionHandle::simulate_frame
    );
    ClassDB::bind_method(D_METHOD("journal"), &NetwPredictionHandle::journal);
    ClassDB::bind_method(
        D_METHOD("teleport_distances"),
        &NetwPredictionHandle::teleport_distances
    );
    ClassDB::bind_method(
        D_METHOD("reachability"),
        &NetwPredictionHandle::reachability
    );
    ClassDB::bind_method(D_METHOD("episode"), &NetwPredictionHandle::episode);
    ClassDB::bind_method(
        D_METHOD("episode_digest"),
        &NetwPredictionHandle::episode_digest
    );
    ClassDB::bind_method(
        D_METHOD("tape_transitions"),
        &NetwPredictionHandle::tape_transitions
    );
    ClassDB::bind_method(
        D_METHOD("transition_state_at", "transition"),
        &NetwPredictionHandle::transition_state_at
    );
    ClassDB::bind_method(
        D_METHOD("record_server_input", "tick", "input"),
        &NetwPredictionHandle::record_server_input
    );
    ClassDB::bind_method(
        D_METHOD("has_consumed_state_tick", "state_tick"),
        &NetwPredictionHandle::has_consumed_state_tick
    );
    ClassDB::bind_method(
        D_METHOD("notify_contact"),
        &NetwPredictionHandle::notify_contact
    );
    ClassDB::bind_method(
        D_METHOD("history_record_tick", "fallback"),
        &NetwPredictionHandle::history_record_tick
    );
    ClassDB::bind_method(
        D_METHOD("stamp_episode"),
        &NetwPredictionHandle::stamp_episode
    );
    ClassDB::bind_method(
        D_METHOD("set_simulated_by", "subject", "enabled", "predictor"),
        &NetwPredictionHandle::set_simulated_by,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("simulated_by_count"),
        &NetwPredictionHandle::simulated_by_count
    );
    ClassDB::bind_method(
        D_METHOD("predicted_command_callable"),
        &NetwPredictionHandle::predicted_command_callable
    );
    ClassDB::bind_method(
        D_METHOD("restate_declaration"),
        &NetwPredictionHandle::restate_declaration
    );

    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("role_for_axes", "source", "mode"),
        &NetwPredictionHandle::role_for_axes
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("resolve_correction_mode_for", "body", "mode", "solves"),
        &NetwPredictionHandle::resolve_correction_mode_for,
        DEFVAL(false)
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("divergence", "predicted", "authoritative", "angles"),
        &NetwPredictionHandle::divergence,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD(
            "divergence_by_field",
            "predicted",
            "authoritative",
            "out",
            "angles"
        ),
        &NetwPredictionHandle::divergence_by_field,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("triggers", "error", "tolerance"),
        &NetwPredictionHandle::triggers
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD(
            "diverged",
            "predicted",
            "authoritative",
            "epsilon",
            "overrides",
            "excludes",
            "angles"
        ),
        &NetwPredictionHandle::diverged,
        DEFVAL(Dictionary()),
        DEFVAL(Dictionary())
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("value_error", "a", "b"),
        &NetwPredictionHandle::value_error
    );
    ClassDB::bind_static_method(
        "NetwPredictionHandle",
        D_METHOD("field_error", "a", "b", "is_angle"),
        &NetwPredictionHandle::field_error
    );

    ClassDB::bind_method(
        D_METHOD("get_simulate"),
        &NetwPredictionHandle::get_simulate
    );
    ClassDB::bind_method(
        D_METHOD("set_simulate", "value"),
        &NetwPredictionHandle::set_simulate
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "simulate"),
        "set_simulate",
        "get_simulate"
    );
    ClassDB::bind_method(
        D_METHOD("get_schedule"),
        &NetwPredictionHandle::get_schedule
    );
    ClassDB::bind_method(
        D_METHOD("set_schedule", "value"),
        &NetwPredictionHandle::set_schedule
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "schedule"),
        "set_schedule",
        "get_schedule"
    );
    ClassDB::bind_method(
        D_METHOD("get_correction_mode"),
        &NetwPredictionHandle::get_correction_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_correction_mode", "value"),
        &NetwPredictionHandle::set_correction_mode
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "correction_mode"),
        "set_correction_mode",
        "get_correction_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_snap_restore"),
        &NetwPredictionHandle::get_snap_restore
    );
    ClassDB::bind_method(
        D_METHOD("set_snap_restore", "value"),
        &NetwPredictionHandle::set_snap_restore
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "snap_restore"),
        "set_snap_restore",
        "get_snap_restore"
    );
    ClassDB::bind_method(
        D_METHOD("get_input_source"),
        &NetwPredictionHandle::get_input_source
    );
    ClassDB::bind_method(
        D_METHOD("set_input_source", "value"),
        &NetwPredictionHandle::set_input_source
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "input_source"),
        "set_input_source",
        "get_input_source"
    );
    ClassDB::bind_method(
        D_METHOD("get_sim_mode"),
        &NetwPredictionHandle::get_sim_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_sim_mode", "value"),
        &NetwPredictionHandle::set_sim_mode
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "sim_mode"),
        "set_sim_mode",
        "get_sim_mode"
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
        PropertyInfo(Variant::INT, "recovery_policy"),
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
        PropertyInfo(Variant::INT, "breach_response"),
        "set_breach_response",
        "get_breach_response"
    );
    ClassDB::bind_method(
        D_METHOD("get_island"),
        &NetwPredictionHandle::get_island
    );
    ClassDB::bind_method(
        D_METHOD("set_island", "value"),
        &NetwPredictionHandle::set_island
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "island",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPredictIsland",
            PROPERTY_USAGE_NONE
        ),
        "set_island",
        "get_island"
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
        D_METHOD("get_max_restore_ticks"),
        &NetwPredictionHandle::get_max_restore_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_max_restore_ticks", "value"),
        &NetwPredictionHandle::set_max_restore_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_restore_ticks"),
        "set_max_restore_ticks",
        "get_max_restore_ticks"
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
        PropertyInfo(Variant::INT, "missing_policy"),
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
        PropertyInfo(Variant::INT, "reconcile_mode"),
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
        PropertyInfo(Variant::INT, "archetype"),
        "set_archetype",
        "get_archetype"
    );

    ClassDB::bind_method(
        D_METHOD("get_last_field_divergence"),
        &NetwPredictionHandle::get_last_field_divergence
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "last_field_divergence"),
        godot::String(),
        "get_last_field_divergence"
    );
    ClassDB::bind_method(
        D_METHOD("get_field_recovery"),
        &NetwPredictionHandle::get_field_recovery
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "field_recovery"),
        godot::String(),
        "get_field_recovery"
    );
    ClassDB::bind_method(
        D_METHOD("get_last_compare_staleness"),
        &NetwPredictionHandle::get_last_compare_staleness
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "last_compare_staleness"),
        godot::String(),
        "get_last_compare_staleness"
    );
    ClassDB::bind_method(
        D_METHOD("get_last_tier_errors"),
        &NetwPredictionHandle::get_last_tier_errors
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "last_tier_errors"),
        godot::String(),
        "get_last_tier_errors"
    );
    ClassDB::bind_method(
        D_METHOD("get_last_verdict_reason"),
        &NetwPredictionHandle::get_last_verdict_reason
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "last_verdict_reason"),
        godot::String(),
        "get_last_verdict_reason"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_reconciling"),
        &NetwPredictionHandle::get_is_reconciling
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_reconciling"),
        godot::String(),
        "get_is_reconciling"
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
    ClassDB::bind_method(
        D_METHOD("get_acknowledged_tick"),
        &NetwPredictionHandle::get_acknowledged_tick
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "acknowledged_tick"),
        godot::String(),
        "get_acknowledged_tick"
    );
    ClassDB::bind_method(
        D_METHOD("get_last_attribution"),
        &NetwPredictionHandle::get_last_attribution
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "last_attribution"),
        godot::String(),
        "get_last_attribution"
    );
    ClassDB::bind_method(
        D_METHOD("get_last_attributed_transition"),
        &NetwPredictionHandle::get_last_attributed_transition
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "last_attributed_transition"),
        godot::String(),
        "get_last_attributed_transition"
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
        PropertyInfo(Variant::INT, "attribution")
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
        PropertyInfo(Variant::INT, "attribution")
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
