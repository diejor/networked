#include "netw/api/predict_slot_engine.hpp"

#include <algorithm>

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/math.hpp"
#include "godot/os.hpp"
#include "godot/utility.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/api/predict_runner.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int ARRIVAL_BUCKET_MAX = NetwPredictStats::ARRIVAL_BUCKETS - 1;
constexpr int REPLAY_DEPTH_BUCKET_MAX
    = NetwPredictStats::REPLAY_DEPTH_BUCKETS - 1;

const String &env_raw_fingerprints() {
    static const String name("NETW_PREDICT_RAW_FP");
    return name;
}

const StringName &seam_predict_drive() {
    static const StringName name("_predict_drive");
    return name;
}

const StringName &seam_predict_consume() {
    static const StringName name("_predict_consume");
    return name;
}

const StringName &seam_predict_evaluate() {
    static const StringName name("_predict_evaluate");
    return name;
}

const StringName &seam_predict_recover() {
    static const StringName name("_predict_recover");
    return name;
}

const StringName &field_native_core() {
    static const StringName name("_native_core");
    return name;
}

const StringName &signal_control_changed() {
    static const StringName name("control_changed");
    return name;
}

const StringName &signal_state_evaluated() {
    static const StringName name("state_evaluated");
    return name;
}

const StringName &key_label() {
    static const StringName name("label");
    return name;
}

const StringName &key_fresh() {
    static const StringName name("fresh");
    return name;
}

const StringName &key_transition() {
    static const StringName name("transition");
    return name;
}

const StringName &key_tick() {
    static const StringName name("tick");
    return name;
}

const StringName &key_ack() {
    static const StringName name("ack");
    return name;
}

const StringName &key_payload() {
    static const StringName name("payload");
    return name;
}

const StringName &key_whole() {
    static const StringName name("whole");
    return name;
}

const StringName &key_samples() {
    static const StringName name("samples");
    return name;
}

const StringName &key_predicted() {
    static const StringName name("predicted");
    return name;
}

const StringName &key_domain() {
    static const StringName name("domain");
    return name;
}

const StringName &key_row_flags() {
    static const StringName name("row_flags");
    return name;
}

const StringName &key_episode_state() {
    static const StringName name("episode_state");
    return name;
}

const StringName &key_probation() {
    static const StringName name("probation");
    return name;
}

const StringName &key_reconstructed() {
    static const StringName name("reconstructed");
    return name;
}

const StringName &key_divergence() {
    static const StringName name("divergence");
    return name;
}

const StringName &key_corrected() {
    static const StringName name("corrected");
    return name;
}

const StringName &key_settled() {
    static const StringName name("settled");
    return name;
}

const StringName &key_meter() {
    static const StringName name("meter");
    return name;
}

const StringName &key_origin() {
    static const StringName name("origin");
    return name;
}

const StringName &key_breach() {
    static const StringName name("breach");
    return name;
}

const StringName &key_witness_fp() {
    static const StringName name("witness_fp");
    return name;
}

const StringName &key_demoted() {
    static const StringName name("demoted");
    return name;
}

const StringName &key_message() {
    static const StringName name("message");
    return name;
}

const StringName &key_severity() {
    static const StringName name("severity");
    return name;
}

const StringName &severity_error() {
    static const StringName name("error");
    return name;
}

bool role_drives_island(int p_role) {
    return p_role == NetwPredict::ROLE_PREDICT
        || p_role == NetwPredict::ROLE_CONSUME
        || p_role == NetwPredict::ROLE_HOST_LOCAL;
}

} // namespace

NetwMultiplayer *NetwPredictSlotEngine::core_seated() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

NetwMultiplayer *NetwPredictSlotEngine::core() const {
    return core_seated();
}

NetwPredictionEngine *NetwPredictSlotEngine::seated_pool() const {
    return pool;
}

Ref<NetwEntity> NetwPredictSlotEngine::seated_entity() const {
    return entity;
}

int64_t NetwPredictSlotEngine::native_slot() const {
    return pool != nullptr ? pool->slot_of(entity) : -1;
}

NetwPredictSlotEngine *NetwPredictSlotEngine::sibling(
    const Ref<NetwEntity> &p_member
) const {
    NetwMultiplayer *seated = core_seated();
    if (seated == nullptr || p_member.is_null()) {
        return nullptr;
    }
    return seated->predict_engine_for(p_member->get_rid_handle());
}

bool NetwPredictSlotEngine::overrides_seam(const StringName &p_seam) const {
    NetwMultiplayer *seated = core_seated();
    return seated != nullptr && seated->overrides_seam(p_seam);
}

NetwPredictSlotEngine::Declaration NetwPredictSlotEngine::declaration_of(
    const Ref<NetwEntity> &p_entity
) const {
    Declaration decided;
    if (p_entity.is_null()) {
        return decided;
    }
    decided.state = p_entity->get_state_binding();
    decided.input = p_entity->get_input_binding();
    decided.authority = p_entity->get_is_authority();
    decided.controlled_locally = p_entity->get_is_controlled_locally()
        || (decided.authority && p_entity->get_controller() == 0);
    return decided;
}

NetwPredictSlotEngine::Declaration NetwPredictSlotEngine::
    resolved_declaration() const {
    if (entity.is_valid()) {
        return declaration_of(entity);
    }
    return declaration;
}

int NetwPredictSlotEngine::role() const {
    return pool != nullptr ? pool->role_of(native_slot())
                           : int(NetwPredict::ROLE_REMOTE);
}

void NetwPredictSlotEngine::set_role_column(int p_role) {
    if (pool != nullptr) {
        pool->set_role(native_slot(), p_role);
    }
}

int NetwPredictSlotEngine::correction() const {
    return pool != nullptr ? pool->correction_of(native_slot())
                           : int(NetwPredict::CORRECTION_MODE_REPLAY);
}

void NetwPredictSlotEngine::set_correction_column(int p_correction) {
    if (pool != nullptr) {
        pool->set_correction(native_slot(), p_correction);
    }
}

Ref<NetwPropertySetBinding> NetwPredictSlotEngine::state_binding() const {
    return pool != nullptr ? pool->state_binding_of(native_slot())
                           : Ref<NetwPropertySetBinding>();
}

Ref<NetwPropertySetBinding> NetwPredictSlotEngine::input_binding() const {
    return pool != nullptr ? pool->input_binding_of(native_slot())
                           : Ref<NetwPropertySetBinding>();
}

Ref<NetwTimeline> NetwPredictSlotEngine::timeline() const {
    return pool != nullptr ? pool->timeline_of(native_slot())
                           : Ref<NetwTimeline>();
}

Ref<NetwTimeline> NetwPredictSlotEngine::entry_history() const {
    return pool != nullptr ? pool->entry_history(native_slot())
                           : Ref<NetwTimeline>();
}

double NetwPredictSlotEngine::tick_delta() const {
    return pool != nullptr ? pool->tick_delta_of(native_slot()) : 1.0 / 60.0;
}

bool NetwPredictSlotEngine::fallback_latched() const {
    return pool != nullptr && pool->fallback_latched_of(native_slot());
}

int64_t NetwPredictSlotEngine::schedule() const {
    const int64_t seat = pool != nullptr ? native_slot() : -1;
    const int seated = seat >= 0 ? pool->schedule_of(seat) : -1;
    if (seated >= 0) {
        return seated;
    }
    return handle.is_valid() ? handle->get_schedule()
                             : int(NetwPredict::SCHEDULE_TICK);
}

int64_t NetwPredictSlotEngine::ack() const {
    return pool != nullptr ? pool->ack_of(native_slot()) : -1;
}

bool NetwPredictSlotEngine::ack_advanced() const {
    return pool != nullptr && pool->ack_advanced_of(native_slot());
}

int64_t NetwPredictSlotEngine::route() const {
    NetwMultiplayer *seated = core_seated();
    if (seated == nullptr || entity.is_null()) {
        return -1;
    }
    return seated->liveness_route_of(entity.ptr());
}

void NetwPredictSlotEngine::attach(
    Object *p_iface,
    const Ref<NetwEntity> &p_entity
) {
    NETW_ZONE_NC("predict engine attach", colors::PREDICTION);
    if (p_iface == nullptr || p_entity.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: attach declined, shell %d entity %d",
            int(p_iface != nullptr),
            int(p_entity.is_valid())
        );
        return;
    }
    NetwMultiplayer *seated = Object::cast_to<NetwMultiplayer>(
        Object::cast_to<Object>(p_iface->get(field_native_core()))
    );
    if (seated == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: the shell seats no session, attach declined"
        );
        return;
    }
    core_id = gd::instance_id(seated);
    pool = seated->get_prediction_engine();
    entity = p_entity;
    handle = p_entity->get_prediction();
    if (pool == nullptr || handle.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: attach found pool %d handle %d",
            int(pool != nullptr),
            int(handle.is_valid())
        );
        return;
    }
    apply_scene_island_defaults();
    handle->get_stats()->bind_slot(pool, p_entity);
    const Callable on_control = callable_mp(
        handle.ptr(),
        &NetwPredictionHandle::seat_control_changed
    );
    if (!p_entity->is_connected(signal_control_changed(), on_control)) {
        p_entity->connect(signal_control_changed(), on_control);
    }
    rewire_on(resolved_declaration());
}

void NetwPredictSlotEngine::release() {
    NETW_ZONE_NC("predict engine release", colors::PREDICTION);
    if (pool == nullptr) {
        NETW_TRACE(sys::PREDICTION, "predict engine: release found no pool");
        return;
    }
    refresh_simulation_gate(true);
    clear_island_promotions();
    pool->clear_simulation_subjects(native_slot());
    unregister_from_loop();
    const predict::Feed feed;
    const Ref<NetwPropertySetBinding> state = state_binding();
    if (state.is_valid()) {
        state->apply_state_feed(feed);
    }
    const Ref<NetwPropertySetBinding> input = input_binding();
    if (input.is_valid()) {
        input->apply_input_feed(feed);
    }
    if (entity.is_null()) {
        return;
    }
    const Callable on_control = callable_mp(
        handle.ptr(),
        &NetwPredictionHandle::seat_control_changed
    );
    if (entity->is_connected(signal_control_changed(), on_control)) {
        entity->disconnect(signal_control_changed(), on_control);
    }
}

void NetwPredictSlotEngine::refresh_simulation_gate(bool p_force_release) {
    pool->refresh_simulation_gate(native_slot(), p_force_release);
}

void NetwPredictSlotEngine::rewire() {
    rewire_on(resolved_declaration());
}

void NetwPredictSlotEngine::publish_topology_roster() {
    pool->publish_topology_roster(native_slot(), handle->get_island());
}

void NetwPredictSlotEngine::network_tick(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict engine network tick", colors::PREDICTION);
    adopt_timing(p_timing);
    if (schedule() == NetwPredict::SCHEDULE_TICK) {
        simulate_tick(p_timing);
        return;
    }
    switch (role()) {
        case NetwPredict::ROLE_PREDICT:
            predict_author_tick(p_timing.get_tick());
            break;
        case NetwPredict::ROLE_HOST_LOCAL:
            host_local_author_tick(p_timing.get_tick());
            break;
        case NetwPredict::ROLE_REMOTE:
            if (fallback_latched()) {
                fallback_author_tick(p_timing.get_tick());
            }
            break;
        default:
            break;
    }
}

void NetwPredictSlotEngine::simulate_tick(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict engine simulate tick", colors::PREDICTION);
    adopt_timing(p_timing);
    switch (role()) {
        case NetwPredict::ROLE_PREDICT:
            predict_step(p_timing.get_delta(), p_timing.get_tick());
            break;
        case NetwPredict::ROLE_CONSUME:
            consume_step(p_timing.get_delta(), p_timing.get_tick());
            break;
        case NetwPredict::ROLE_HOST_LOCAL:
            host_local_step(p_timing.get_delta(), p_timing.get_tick());
            break;
        case NetwPredict::ROLE_REMOTE:
            if (fallback_latched()) {
                fallback_author_step(p_timing.get_tick());
            }
            break;
        case NetwPredict::ROLE_SIMULATE:
            simulated_step(p_timing.get_delta(), p_timing.get_tick());
            break;
        default:
            break;
    }
}

void NetwPredictSlotEngine::simulate_frame(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict engine simulate frame", colors::PREDICTION);
    adopt_timing(p_timing);
    if (schedule() != NetwPredict::SCHEDULE_FRAME) {
        return;
    }
    simulate_solver(p_timing);
}

void NetwPredictSlotEngine::simulate_stepped(
    const NetwPredictTiming &p_timing
) {
    NETW_ZONE_NC("predict engine simulate stepped", colors::PREDICTION);
    adopt_timing(p_timing);
    if (schedule() != NetwPredict::SCHEDULE_STEPPED) {
        return;
    }
    simulate_solver(p_timing);
}

void NetwPredictSlotEngine::simulate_solver(const NetwPredictTiming &p_timing) {
    const int64_t frame_floor
        = pool->last_frame_transition_tick_of(native_slot());
    switch (role()) {
        case NetwPredict::ROLE_PREDICT:
            if (!p_timing.get_simulating()
                || p_timing.get_tick() <= frame_floor) {
                charge_authoring_clamp();
                send_command_frame();
                return;
            }
            predict_frame_step(p_timing);
            break;
        case NetwPredict::ROLE_CONSUME:
            consume_frame_step(p_timing);
            break;
        case NetwPredict::ROLE_HOST_LOCAL:
            host_local_frame_step(p_timing);
            break;
        case NetwPredict::ROLE_REMOTE:
            if (fallback_latched()) {
                if (p_timing.get_tick() <= frame_floor) {
                    charge_authoring_clamp();
                    send_command_frame();
                    return;
                }
                fallback_author_frame_step(p_timing);
            }
            break;
        case NetwPredict::ROLE_SIMULATE:
            if (p_timing.get_simulating()) {
                simulated_frame_step(p_timing);
            }
            break;
        default:
            break;
    }
}

void NetwPredictSlotEngine::prepare_island(int p_schedule) {
    if (!island_pass_admits(p_schedule, schedule())
        || !role_drives_island(role())) {
        return;
    }
    pool->refresh_island_membership(
        native_slot(),
        handle->get_island(),
        role() == NetwPredict::ROLE_PREDICT,
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_reconcile_mode)
    );
}

void NetwPredictSlotEngine::adopt_timing(const NetwPredictTiming &p_timing) {
    const int64_t slot = native_slot();
    if (p_timing.get_ticktime() > 0.0) {
        pool->set_tick_delta(slot, p_timing.get_ticktime());
    }
    pool->set_frame_index(slot, p_timing.get_frame());
    pool->set_declared_quantum(slot, std::max(1, p_timing.get_quantum()));
}

void NetwPredictSlotEngine::finalize_frame_state() {
    NETW_ZONE_NC("predict finalize frame state", colors::PREDICTION);
    if (schedule() != NetwPredict::SCHEDULE_FRAME
        || role() != NetwPredict::ROLE_PREDICT) {
        return;
    }
    finalize_solver_state();
}

void NetwPredictSlotEngine::finalize_stepped_state() {
    NETW_ZONE_NC("predict finalize stepped state", colors::PREDICTION);
    if (schedule() != NetwPredict::SCHEDULE_STEPPED
        || role() != NetwPredict::ROLE_PREDICT) {
        return;
    }
    finalize_solver_state();
}

void NetwPredictSlotEngine::finalize_solver_state() {
    const int64_t slot = native_slot();
    const Dictionary state = capture();
    const int64_t driven_entry = pool->last_driven_entry_index_of(slot);
    if (driven_entry > pool->last_recorded_entry_index_of(slot)) {
        const Ref<NetwTimeline> entries = entry_history();
        if (entries.is_valid()) {
            entries->record_state(driven_entry + 1, state);
        } else {
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: entry %d has no history to record into",
                int(driven_entry)
            );
        }
        close_journal_row(driven_entry, state);
        pool->set_last_recorded_entry_index(slot, driven_entry);
    }
    const int64_t driven_tick = pool->last_driven_input_tick_of(slot);
    if (driven_tick > pool->last_recorded_input_tick_of(slot)) {
        const Ref<NetwTimeline> held = timeline();
        if (held.is_valid()) {
            held->record_state(driven_tick + 1, state);
        } else {
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: tick %d has no timeline to record into",
                int(driven_tick)
            );
        }
        pool->set_last_recorded_input_tick(slot, driven_tick);
    }
}

bool NetwPredictSlotEngine::uses_schedule(int p_schedule) const {
    return schedule() == p_schedule;
}

Dictionary NetwPredictSlotEngine::transition_state_at(
    int64_t p_entry_index
) const {
    if (schedule() == NetwPredict::SCHEDULE_TICK) {
        const Ref<NetwTimeline> held = timeline();
        return held.is_valid() ? held->state_at(p_entry_index + 1)
                               : Dictionary();
    }
    const Ref<NetwTimeline> entries = entry_history();
    return entries.is_valid() ? entries->state_at(p_entry_index + 1)
                              : Dictionary();
}

void NetwPredictSlotEngine::charge_speculation_hold() {
    if (core_seated() != nullptr) {
        pool->record_speculation_hold(native_slot());
    }
}

void NetwPredictSlotEngine::charge_authoring_clamp() {
    if (core_seated() != nullptr) {
        pool->record_authoring_clamp(native_slot());
    }
}

void NetwPredictSlotEngine::sync_episode() {
    pool->sync_episode(native_slot());
}

bool NetwPredictSlotEngine::defer_operator_for_witness(
    int64_t p_recv_tick,
    int64_t p_basis,
    const Dictionary &p_payload
) {
    const int verdict = pool->defer_operator_for_witness(
        native_slot(),
        p_recv_tick,
        p_basis,
        p_payload,
        handle->get_transport_corridor().is_valid(),
        handle->resolved_recovery_policy()
            == NetwPredict::RECOVERY_POLICY_OBSERVE
    );
    if (verdict == NetwPredictionEngine::DEFER_DISPLACED) {
        sync_episode();
    }
    return verdict == NetwPredictionEngine::DEFER_HELD;
}

void NetwPredictSlotEngine::retry_deferred_operator(int64_t p_basis) {
    const int64_t slot = native_slot();
    if (pool->deferred_operator_basis(slot) != p_basis) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: basis %d holds no deferred operator",
            int(p_basis)
        );
        return;
    }
    const int64_t recv_tick = pool->deferred_operator_recv_tick(slot);
    on_state(recv_tick, p_basis, pool->release_deferred_operator(slot));
}

void NetwPredictSlotEngine::enter_fallback_at(
    int64_t p_transition,
    int p_attribution
) {
    enter_fallback(p_transition, p_attribution, false);
}

void NetwPredictSlotEngine::enter_fallback(
    int64_t p_transition,
    int p_attribution,
    bool p_demoted
) {
    NETW_ZONE_NC("predict engine enter fallback", colors::PREDICTION);
    const int64_t slot = native_slot();
    const bool stream_was_reconstructed = pool->stream_reconstructed_of(slot);
    pool->set_fallback_latched(slot, true);
    if (core_seated() != nullptr) {
        pool->enter_quarantine(
            slot,
            p_transition,
            stream_was_reconstructed,
            p_attribution,
            p_demoted
        );
    }
    sync_episode();
    Dictionary extra;
    extra[key_demoted()] = p_demoted;
    pool->announce_episode(slot, EventPlane::EPISODE_FALLBACK, extra);
    rewire_on(resolved_declaration());
}

void NetwPredictSlotEngine::on_quarantine_state_frame(
    const Dictionary &p_header
) {
    if (!fallback_latched() || core_seated() == nullptr) {
        return;
    }
    const predict::WritePlan plan = pool->quarantine_state(
        native_slot(),
        int64_t(p_header.get(key_tick(), -1)),
        int64_t(p_header.get(key_ack(), -1)),
        state_columns(p_header.get(key_payload(), Dictionary())),
        bool(p_header.get(key_whole(), true))
    );
    sync_episode();
    begin_reseed(plan);
}

void NetwPredictSlotEngine::apply_quarantine_witness(int64_t p_basis) {
    if (!fallback_latched() || core_seated() == nullptr) {
        return;
    }
    const int64_t slot = native_slot();
    const predict::WritePlan plan = pool->quarantine_witness(
        slot,
        p_basis,
        pool->authority_witness_class(slot, p_basis)
    );
    sync_episode();
    begin_reseed(plan);
}

void NetwPredictSlotEngine::begin_reseed(const predict::WritePlan &p_plan) {
    if (p_plan.skip) {
        return;
    }
    const int64_t slot = native_slot();
    const int64_t basis = p_plan.basis;
    const RecoveryPlan staged = plan_of(p_plan);
    restore(
        staged.write,
        NetwPredictJournal::Operator::RESEED,
        basis,
        nullptr,
        true,
        true
    );
    const Dictionary reseed_provenance
        = pool->pending_provenance(slot).duplicate(true);
    pool->set_fallback_latched(slot, false);
    rewire_on(resolved_declaration());
    pool->set_pending_provenance(native_slot(), reseed_provenance);
    sync_episode();
}

void NetwPredictSlotEngine::finish_reseed_alignment(
    int64_t p_recv_tick,
    int64_t p_ack,
    const Dictionary &p_payload
) {
    pool->finish_reseed_alignment(
        native_slot(),
        p_recv_tick,
        p_ack,
        p_payload,
        witness_sample(),
        island_participant_ids(),
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_breach)
    );
}

void NetwPredictSlotEngine::send_command_frame() {
    pool->send_command_frame(native_slot());
}

void NetwPredictSlotEngine::send_ack_frame() {
    pool->send_ack_frame(native_slot());
}

void NetwPredictSlotEngine::receive_relayed_command_frame(
    const PackedByteArray &p_payload
) {
    pool->admit_relayed_payload(native_slot(), p_payload);
}

void NetwPredictSlotEngine::receive_command_frame(
    const PackedByteArray &p_payload
) {
    pool->admit_command_payload(native_slot(), p_payload);
}

void NetwPredictSlotEngine::file_command_cell(
    int64_t p_transition,
    const Dictionary &p_command,
    int p_origin
) {
    const int64_t slot = native_slot();
    pool->set_newest_matrix_transition(
        slot,
        std::max(pool->newest_matrix_transition_of(slot), p_transition)
    );
    if (core_seated() == nullptr || p_transition < 0) {
        return;
    }
    pool->joint_record(
        slot,
        p_transition,
        Array(),
        p_command,
        false,
        p_origin == NetwPredict::COMMAND_ORIGIN_RELAYED,
        p_origin == NetwPredict::COMMAND_ORIGIN_PREDICTED
    );
}

Dictionary NetwPredictSlotEngine::command_cell_at(int64_t p_transition) const {
    if (core_seated() == nullptr) {
        return Dictionary();
    }
    return pool->command_cell_at(native_slot(), p_transition);
}

void NetwPredictSlotEngine::receive_ack_frame(
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("predict engine receive ack frame", colors::PREDICTION);
    if (role() != NetwPredict::ROLE_PREDICT && !fallback_latched()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: role %d takes no ack frame",
            role()
        );
        return;
    }
    const int64_t slot = native_slot();
    const PackedInt64Array counts = pool->receive_ack_frame(
        slot,
        p_payload,
        pool->tape_epoch_of(slot),
        core_seated() != nullptr,
        callable_mp(
            handle.ptr(),
            &NetwPredictionHandle::seat_quarantine_witness
        ),
        callable_mp(
            handle.ptr(),
            &NetwPredictionHandle::seat_deferred_operator_retry
        )
    );
    const Ref<NetwPredictStats> stats = handle->get_stats();
    const int64_t receipt = counts[NetwPredictionEngine::ACK_RUN_RECEIPT];
    if (receipt == NetwPredictionEngine::ACK_RECEIPT_UNDECODED) {
        stats->set_int_fact(
            NetwPredictStats::FACT_FRAMES_DROPPED_INVALID,
            stats->get_int_fact(NetwPredictStats::FACT_FRAMES_DROPPED_INVALID)
                + 1
        );
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: an ack frame of %d bytes did not decode",
            int(p_payload.size())
        );
        return;
    }
    if (receipt == NetwPredictionEngine::ACK_RECEIPT_FOREIGN_EPOCH) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: an ack frame names a foreign tape epoch"
        );
        return;
    }
    pool->set_ack_domain_confirmed(slot, true);
    stats->set_int_fact(
        NetwPredictStats::FACT_SUBSTITUTED,
        stats->get_int_fact(NetwPredictStats::FACT_SUBSTITUTED)
            + counts[NetwPredictionEngine::ACK_RUN_SUBSTITUTED]
    );
    stats->set_int_fact(
        NetwPredictStats::FACT_ACK_CONFIRMED,
        counts[NetwPredictionEngine::ACK_RUN_OF_ACKS]
    );
    publish_ack_age(counts[NetwPredictionEngine::ACK_RUN_AGE]);
}

void NetwPredictSlotEngine::finalize_recorded_state(
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict finalize recorded state", colors::PREDICTION);
    if (role() == NetwPredict::ROLE_PREDICT) {
        return;
    }
    const int64_t transition = recorded_transition();
    const int64_t fingerprint = state_fingerprint(p_payload);
    close_journal_row(transition, p_payload);
    judge_owner_claim(transition, fingerprint);
}

void NetwPredictSlotEngine::judge_owner_claim(
    int64_t p_transition,
    int64_t p_fingerprint
) {
    pool->report_owner_claim(native_slot(), p_transition, p_fingerprint);
}

int64_t NetwPredictSlotEngine::recorded_transition() const {
    if (schedule() == NetwPredict::SCHEDULE_FRAME
        && role() == NetwPredict::ROLE_CONSUME) {
        return ack();
    }
    return handle->get_stats()->get_int_fact(
        NetwPredictStats::FACT_LAST_DRIVE_LABEL
    );
}

predict::SlotCursors NetwPredictSlotEngine::cursors() const {
    predict::SlotCursors read;
    read.role = role();
    read.schedule = int(schedule());
    read.ack = ack();
    read.ack_advanced = ack_advanced();
    if (pool != nullptr) {
        const int64_t slot = native_slot();
        read.last_replayed_label = pool->last_replayed_label_of(slot);
        read.last_replayed_fresh = pool->last_replayed_fresh_of(slot);
        read.last_driven_input_tick = pool->last_driven_input_tick_of(slot);
    }
    return read;
}

int64_t NetwPredictSlotEngine::order_key() const {
    return predict::order_key_for_route(route());
}

int64_t NetwPredictSlotEngine::history_record_tick(
    int64_t p_fallback_tick
) const {
    return predict::history_record_tick(cursors(), p_fallback_tick);
}

bool NetwPredictSlotEngine::consumed_unslotted_transition() const {
    return predict::consumed_unslotted_transition(cursors());
}

bool NetwPredictSlotEngine::has_consumed_state_tick(
    int64_t p_state_tick
) const {
    return predict::has_consumed_state_tick(cursors(), p_state_tick);
}

int NetwPredictSlotEngine::resolved_correction_mode() const {
    return correction();
}

void NetwPredictSlotEngine::record_server_input(
    int64_t p_tick,
    const Dictionary &p_input
) {
    const Ref<NetwTimeline> held = timeline();
    if (role() != NetwPredict::ROLE_CONSUME || held.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d has no consuming timeline to record on",
            int(p_tick)
        );
        return;
    }
    held->record_input(p_tick, p_input);
    const int64_t slot = native_slot();
    if (pool->next_input_tick_of(slot) < 0) {
        pool->set_next_input_tick(slot, p_tick);
    }
}

int NetwPredictSlotEngine::resolve_correction(int p_declared) const {
    if (core_seated() == nullptr) {
        return NetwPredictionHandle::resolve_correction_mode_for(
            entity.is_valid() ? entity->get_owner() : nullptr,
            static_cast<NetwPredict::CorrectionMode>(p_declared)
        );
    }
    const int64_t slot = native_slot();
    if (slot < 0) {
        return p_declared;
    }
    return pool->resolve_correction(slot, p_declared);
}

void NetwPredictSlotEngine::record_input_to_pool(
    int64_t p_tick,
    const Dictionary &p_input
) {
    pool->record_input_bytes(native_slot(), p_tick, p_input);
}

void NetwPredictSlotEngine::on_control_changed(
    int64_t p_previous_peer,
    int64_t p_peer
) {
    (void)p_previous_peer;
    (void)p_peer;
    rewire_on(resolved_declaration());
}

void NetwPredictSlotEngine::on_reparented() {
    apply_scene_island_defaults();
    rewire_on(resolved_declaration());
}

void NetwPredictSlotEngine::apply_scene_island_defaults() {
    const Ref<NetwPredictIsland> declared = handle->get_island();
    if (declared->get_declared() && !declared->get_inherited()) {
        return;
    }
    const Ref<NetwSceneHandle> scene = entity->get_scene();
    NetwMultiplayer *session = scene.is_valid()
        ? NetwEntity::session_core_for(entity->get_owner())
        : nullptr;
    const RID scene_rid = scene.is_valid() ? scene->get_entity() : RID();
    const Ref<NetwEntity> host = session != nullptr && scene_rid.is_valid()
        ? Object::cast_to<NetwEntity>(session->entity_get_view(scene_rid).ptr())
        : Ref<NetwEntity>();
    Ref<NetwPredictIsland> inherited;
    if (host.is_valid() && host != entity) {
        const Ref<NetwPredictionHandle> host_prediction
            = host->get_prediction();
        if (host_prediction.is_valid()) {
            inherited = host_prediction->get_island()->inheritable();
        }
    }
    if (inherited.is_null() && !declared->get_inherited()) {
        return;
    }
    handle->set_island(inherited);
}

void NetwPredictSlotEngine::rewire_on(const Declaration &p_declaration) {
    NETW_ZONE_NC("predict engine rewire", colors::PREDICTION);
    declaration = p_declaration;
    if (entity.is_null() || entity->get_owner() == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: rewire found no owning node, staying unwired"
        );
        return;
    }
    const Ref<NetwPropertySetBinding> state = p_declaration.state;
    const Ref<NetwPropertySetBinding> input = p_declaration.input;
    if (state.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: rewire found no state binding, staying unwired"
        );
        return;
    }
    const int64_t slot = native_slot();
    const Ref<NetwPropertySetBinding> seated = state_binding();
    const bool was_wired = seated.is_valid();
    const bool same_stream = seated == state;
    pool->bind_property_sets(slot, state, input);
    pool->declare_axes(
        slot,
        p_declaration.authority,
        p_declaration.controlled_locally,
        input.is_null()
    );
    pool->set_declared_epoch(slot, handle->get_epoch());

    unregister_from_loop();
    pool->bind_timeline(slot, Ref<NetwTimeline>());
    pool->reset_for_rewire(
        slot,
        was_wired,
        same_stream,
        !OS::get_singleton()
             ->get_environment(env_raw_fingerprints())
             .is_empty(),
        input.is_valid() ? input->snapshot_payload() : Dictionary()
    );
    sync_episode();
    refresh_simulation_gate(false);
    publish_topology_roster();
    const Ref<NetwPredictStats> stats = handle->get_stats();
    stats->set_int_fact(NetwPredictStats::FACT_COMMAND_QUEUE_DEPTH, 0);
    stats->set_int_fact(NetwPredictStats::FACT_ACK_CONFIRMED, -1);
    stats->set_int_fact(NetwPredictStats::FACT_TAPE_EPOCH, -1);
    stats->set_int_fact(NetwPredictStats::FACT_TAPE_INDEX, -1);
    stats->set_int_fact(NetwPredictStats::FACT_TAPE_QUEUE_DEPTH, 0);

    NetwMultiplayer *session = core_seated();
    if (session != nullptr) {
        adopt_timing(session->frame_timing());
    }
    const int resolved_role = resolve_axes();
    set_role_column(resolved_role);
    if (resolved_role != NetwPredict::ROLE_PREDICT) {
        clear_island_promotions();
    }
    set_correction_column(resolve_correction(handle->get_correction_mode()));
    pool->seed_recovery_ledger(slot);
    pool->validate_declaration(
        slot,
        state->get_set(),
        callable_mp(
            handle.ptr(),
            &NetwPredictionHandle::seat_reachability_findings
        )
    );
    if (session != nullptr) {
        const int resolved = pool->adopt_declaration(
            entity,
            state,
            input_binding(),
            handle->get_schedule(),
            resolved_role,
            handle->get_correction_mode(),
            handle->get_snap_restore(),
            handle->get_max_restore_ticks(),
            pool_island(),
            handle->get_witness_contacts().is_valid()
        );
        if (resolved >= 0) {
            set_correction_column(resolved);
        }
        pool->tape_reset(slot, pool->tape_epoch_of(slot));
        pool->push_carry_rules(slot);
        reconfigure();
    }
    const predict::Feed feed = pool->role_feed(
        resolved_role,
        fallback_latched(),
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_state_frame),
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_input_frame),
        callable_mp(
            handle.ptr(),
            &NetwPredictionHandle::seat_simulated_state_frame
        ),
        callable_mp(
            handle.ptr(),
            &NetwPredictionHandle::seat_quarantine_state_frame
        )
    );
    if (pool->wire_role_timeline(
            slot,
            resolved_role,
            fallback_latched(),
            declaration.timeline
        )) {
        register_with_loop();
    }
    state->apply_state_feed(feed);
    if (input.is_valid()) {
        input->apply_input_feed(feed);
    }
    if (session != nullptr) {
        session->display_mark_role_dirty(entity->get_rid_handle());
    }
}

void NetwPredictSlotEngine::reconfigure() {
    if (core_seated() != nullptr) {
        pool->reconfigure_from(native_slot(), handle);
    }
}

void NetwPredictSlotEngine::emit_reachability_findings(
    const Array &p_findings
) {
    for (int64_t at = 0; at < p_findings.size(); ++at) {
        const Dictionary finding = p_findings[at];
        const String message = String(finding.get(key_message(), String()));
        const StringName severity
            = StringName(finding.get(key_severity(), StringName()));
        if (severity == severity_error()) {
            NETW_ERROR(sys::PREDICTION, "%s", message.utf8().get_data());
        } else {
            NETW_WARN(sys::PREDICTION, "%s", message.utf8().get_data());
        }
    }
}

int NetwPredictSlotEngine::pool_island() const {
    if (handle->get_reconcile_mode() == NetwPredict::RECONCILE_JOINT) {
        return NetwPredictionEngine::ISLAND_JOINT;
    }
    if (handle->get_island()->get_declared()
        || pool->simulation_subject_count(native_slot()) > 0) {
        return NetwPredictionEngine::ISLAND_DECLARED;
    }
    return NetwPredictionEngine::ISLAND_NONE;
}

int NetwPredictSlotEngine::resolve_axes() {
    return pool->resolve_axes(native_slot(), handle);
}

void NetwPredictSlotEngine::notify_contact() {
    pool->notify_contact(
        native_slot(),
        handle->get_island(),
        handle->get_witness_contacts().is_valid(),
        handle->get_collision_cooldown_ticks()
    );
}

void NetwPredictSlotEngine::admit_reconcile_mode() {
    pool->admit_reconcile_mode(native_slot());
}

void NetwPredictSlotEngine::follow_relay_subscription(int p_mode) {
    pool->follow_relay_subscription(native_slot(), p_mode);
}

void NetwPredictSlotEngine::clear_island_promotions() {
    pool->clear_island_promotions(native_slot());
}

void NetwPredictSlotEngine::predict_author_tick(int64_t p_tick) {
    const int64_t slot = native_slot();
    const int64_t latest = pool->latest_input_tick_of(slot);
    if (latest > pool->last_driven_input_tick_of(slot)) {
        const Ref<NetwTimeline> held = timeline();
        if (held.is_valid()) {
            held->record_state(latest + 1, capture());
        } else {
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: tick %d authors with no timeline",
                int(latest)
            );
        }
        pool->set_last_recorded_input_tick(slot, latest);
    }
    pool->set_frame_input(slot, pool->author_input(slot, p_tick));
}

void NetwPredictSlotEngine::predict_frame_step(
    const NetwPredictTiming &p_timing
) {
    NETW_ZONE_NC("predict engine predict frame step", colors::PREDICTION);
    const int64_t slot = native_slot();
    pool->set_last_frame_transition_tick(slot, p_timing.get_tick());
    if (speculation_horizon_full()) {
        charge_speculation_hold();
        send_command_frame();
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d is past the speculation horizon",
            int(p_timing.get_tick())
        );
        return;
    }
    const bool overridden = overrides_seam(seam_predict_drive());
    Fold fold;
    if (overridden) {
        fold = drive_fold(p_timing.get_tick());
    }
    const Dictionary drove = record_drive(
        pool->next_tape_entry_index_of(slot),
        overridden ? fold.label : -1,
        overridden ? int(fold.kind) : int(NetwPredict::DRIVE_KIND_NONE),
        pool->frame_input_of(slot),
        p_timing.get_tick(),
        overridden,
        true
    );
    if (drove.is_empty()) {
        send_command_frame();
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d opened no transition",
            int(p_timing.get_tick())
        );
        return;
    }
    const int64_t label = int64_t(drove[key_label()]);
    const bool fresh = bool(drove[key_fresh()]);
    adopt_tape_position(int64_t(drove[key_transition()]));
    run(pool->frame_input_of(slot), p_timing.get_delta(), label, fresh);
    if (fresh) {
        pool->set_last_driven_input_tick(slot, label);
    }
    send_command_frame();
}

Fold NetwPredictSlotEngine::drive_fold(int64_t p_frame_tick) {
    const int64_t slot = native_slot();
    if (overrides_seam(seam_predict_drive())) {
        NetwMultiplayer *seated = core_seated();
        const Ref<NetwPredictFold> answered = seated->predict_drive(
            pool->latest_input_tick_of(slot),
            pool->last_driven_input_tick_of(slot),
            p_frame_tick
        );
        if (answered.is_valid()) {
            Fold out;
            out.label = answered->label();
            out.fresh = answered->fresh();
            out.kind = DriveKind(answered->kind());
            return out;
        }
        seated->seam_refused(seam_predict_drive(), route(), String("null"));
    }
    return prediction_core::fold(
        pool->latest_input_tick_of(slot),
        pool->last_driven_input_tick_of(slot),
        p_frame_tick
    );
}

int NetwPredictSlotEngine::consume_action(int p_depth, int p_buffer) {
    if (overrides_seam(seam_predict_consume())) {
        return core_seated()->predict_consume(p_depth, p_buffer);
    }
    return prediction_core::consume_action(p_depth, p_buffer);
}

void NetwPredictSlotEngine::fallback_author_tick(int64_t p_tick) {
    predict_author_tick(p_tick);
}

void NetwPredictSlotEngine::fallback_author_frame_step(
    const NetwPredictTiming &p_timing
) {
    const int64_t slot = native_slot();
    pool->set_last_frame_transition_tick(slot, p_timing.get_tick());
    const Fold fold = drive_fold(p_timing.get_tick());
    author_command_entry(fold.label, fold.fresh);
    if (fold.fresh) {
        pool->set_last_driven_input_tick(slot, fold.label);
    }
    send_command_frame();
}

void NetwPredictSlotEngine::fallback_author_step(int64_t p_tick) {
    const int64_t slot = native_slot();
    pool->author_input(slot, p_tick);
    prepare_tick_tape(p_tick);
    author_command_entry(p_tick, true);
    pool->set_last_driven_input_tick(slot, p_tick);
    send_command_frame();
}

void NetwPredictSlotEngine::predict_step(double p_delta, int64_t p_tick) {
    NETW_ZONE_NC("predict engine predict step", colors::PREDICTION);
    const int64_t slot = native_slot();
    const Dictionary input = pool->author_input(slot, p_tick);
    if (speculation_horizon_full()) {
        charge_speculation_hold();
        send_command_frame();
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d is past the speculation horizon",
            int(p_tick)
        );
        return;
    }
    prepare_tick_tape(p_tick);
    adopt_tape_position(p_tick);
    pool->set_last_driven_input_tick(slot, p_tick);
    record_drive(
        p_tick,
        p_tick,
        NetwPredict::DRIVE_KIND_FRESH,
        input,
        p_tick,
        true,
        true,
        true
    );
    run(input, p_delta, p_tick, true);
    const Dictionary state = capture();
    const Ref<NetwTimeline> held = timeline();
    if (held.is_valid()) {
        held->record_state(p_tick + 1, state);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d drove with no timeline to record on",
            int(p_tick)
        );
    }
    close_journal_row(p_tick, state);
    send_command_frame();
}

void NetwPredictSlotEngine::simulated_step(double p_delta, int64_t p_tick) {
    const Dictionary input = predicted_command(p_tick);
    const bool joint
        = handle->get_reconcile_mode() == NetwPredict::RECONCILE_JOINT;
    if (joint) {
        record_drive(
            p_tick,
            p_tick,
            NetwPredict::DRIVE_KIND_SUBSTITUTED,
            input,
            p_tick,
            false
        );
        const Dictionary existing = command_cell_at(p_tick);
        if (int(existing.get(key_origin(), -1))
            != NetwPredict::COMMAND_ORIGIN_RELAYED) {
            file_command_cell(
                p_tick,
                input,
                NetwPredict::COMMAND_ORIGIN_PREDICTED
            );
        }
    } else {
        mark_idle_drive(p_tick, NetwPredict::DRIVE_KIND_SUBSTITUTED);
    }
    run(input, p_delta, p_tick, false);
    const Ref<NetwPredictStats> stats = handle->get_stats();
    stats->set_int_fact(
        NetwPredictStats::FACT_SUBSTITUTED,
        stats->get_int_fact(NetwPredictStats::FACT_SUBSTITUTED) + 1
    );
    if (!joint) {
        return;
    }
    const Dictionary state = capture();
    const Ref<NetwTimeline> held = timeline();
    if (held.is_valid()) {
        held->record_state(p_tick + 1, state);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: joint member has no timeline at tick %d",
            int(p_tick)
        );
    }
    close_journal_row(p_tick, state);
}

void NetwPredictSlotEngine::simulated_frame_step(
    const NetwPredictTiming &p_timing
) {
    const int64_t slot = native_slot();
    if (p_timing.get_tick() <= pool->last_frame_transition_tick_of(slot)) {
        return;
    }
    pool->set_last_frame_transition_tick(slot, p_timing.get_tick());
    simulated_step(p_timing.get_delta(), p_timing.get_tick());
}

Dictionary NetwPredictSlotEngine::predicted_command(int64_t p_tick) {
    return pool->predicted_command(native_slot(), entity, p_tick);
}

void NetwPredictSlotEngine::on_simulated_state_frame(
    const Dictionary &p_header
) {
    pool->admit_simulated_state(native_slot(), p_header);
}

void NetwPredictSlotEngine::prepare_tick_tape(int64_t p_tick) {
    pool->prepare_tick_tape(native_slot(), p_tick);
}

bool NetwPredictSlotEngine::speculation_horizon_full() {
    return pool->speculation_horizon_full(native_slot());
}

void NetwPredictSlotEngine::publish_ack_age(int64_t p_age) {
    if (p_age >= 0) {
        handle->set_ack_age_ticks(int(p_age));
    }
}

void NetwPredictSlotEngine::on_state_frame(const Dictionary &p_header) {
    const int64_t slot = native_slot();
    pool->set_stream_reconstructed(
        slot,
        pool->stream_reconstructed_of(slot)
            || bool(p_header.get(key_whole(), true))
    );
    on_state(
        int64_t(p_header.get(key_tick(), -1)),
        int64_t(p_header.get(key_ack(), -1)),
        p_header.get(key_payload(), Dictionary())
    );
}

void NetwPredictSlotEngine::on_state(
    int64_t p_recv_tick,
    int64_t p_ack,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict engine state comparison", colors::PREDICTION);
    const int64_t slot = native_slot();
    const Dictionary opened = pool->open_state_comparison(
        slot,
        p_recv_tick,
        p_ack,
        p_payload,
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_reseed_alignment)
    );
    if (opened.is_empty()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: the pool opened no comparison at ack %d",
            int(p_ack)
        );
        return;
    }
    const int64_t ack_label = int64_t(opened[key_label()]);
    const Dictionary predicted = opened[key_predicted()];
    const int domain = int(opened[key_domain()]);
    const int64_t row_flags = int64_t(opened[key_row_flags()]);
    const int64_t episode_state_before = int64_t(opened[key_episode_state()]);
    const bool probation_before = bool(opened[key_probation()]);
    const bool reconstructed = bool(opened[key_reconstructed()]);
    double divergence = 0.0;
    bool corrected = false;
    bool settled = false;
    int meter = 0;
    if (reconstructed) {
        const Dictionary judged = pool->judge_state(
            slot,
            p_ack,
            domain,
            row_flags,
            predicted,
            p_payload,
            overrides_seam(seam_predict_evaluate())
                ? callable_mp(
                      handle.ptr(),
                      &NetwPredictionHandle::seat_evaluate_seam
                  )
                : Callable()
        );
        divergence = double(judged[key_divergence()]);
        corrected = bool(judged[key_corrected()]);
        settled = bool(judged[key_settled()]);
        meter = int(judged[key_meter()]);
    }
    const int attribution = pool->attribution_for(slot, p_ack);
    if (pool->settle_comparison(
            slot,
            p_recv_tick,
            p_ack,
            reconstructed,
            corrected,
            settled,
            divergence,
            probation_before,
            episode_state_before,
            callable_mp(handle.ptr(), &NetwPredictionHandle::seat_fallback_at)
        )
        != NetwPredictionEngine::SETTLE_PROCEED) {
        return;
    }
    if (corrected
        && defer_operator_for_witness(p_recv_tick, p_ack, p_payload)) {
        pool->note_verdict_reason(
            slot,
            NetwPredict::VERDICT_REASON_WITNESS_DEFERRED
        );
        handle->emit_signal(
            signal_state_evaluated(),
            p_recv_tick,
            p_ack,
            divergence,
            corrected
        );
        return;
    }
    if (corrected
        && handle->get_reconcile_mode() == NetwPredict::RECONCILE_JOINT) {
        pool->note_joint_basis(
            slot,
            p_ack,
            p_payload,
            NetwPredictionEngine::JOINT_FLOOR_STATE
        );
    } else if (corrected) {
        if (!pool->run_recovery_ladder(
                slot,
                p_ack,
                ack_label,
                predicted,
                p_payload,
                meter,
                domain,
                attribution,
                witness_sample(),
                island_participant_ids(),
                callable_mp(handle.ptr(), &NetwPredictionHandle::seat_breach),
                overrides_seam(seam_predict_recover())
                    ? callable_mp(
                          handle.ptr(),
                          &NetwPredictionHandle::seat_recover_seam
                      )
                    : Callable()
            )) {
            handle->emit_signal(
                signal_state_evaluated(),
                p_recv_tick,
                p_ack,
                divergence,
                corrected
            );
            return;
        }
    }
    if (core_seated() != nullptr) {
        pool->trim_history(slot, p_ack);
    }
    handle->emit_signal(
        signal_state_evaluated(),
        p_recv_tick,
        p_ack,
        divergence,
        corrected
    );
}

Ref<NetwPredictRecovery> NetwPredictSlotEngine::recover_through_seam(
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
    NetwMultiplayer *seated = core_seated();
    if (seated == nullptr) {
        return Ref<NetwPredictRecovery>();
    }
    return seated->predict_recover(
        p_carried,
        p_policy,
        p_correction,
        p_snap_restore,
        p_projection,
        p_before,
        p_tier_errors,
        pool->wiring_snapshot(native_slot()),
        p_context,
        p_tick_delta
    );
}

Ref<NetwPredictJudgement> NetwPredictSlotEngine::evaluate_through_seam(
    NetwPredictJournal::Domain p_domain,
    NetwPredict::ExactVerdict p_exact_verdict,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    const Dictionary &p_field_sink
) {
    NetwMultiplayer *seated = core_seated();
    if (seated == nullptr) {
        return Ref<NetwPredictJudgement>();
    }
    return seated->predict_evaluate(
        p_domain,
        p_exact_verdict,
        p_predicted,
        p_payload,
        pool->wiring_snapshot(native_slot()),
        p_field_sink
    );
}

Dictionary NetwPredictSlotEngine::restore_payload(
    const predict::JointPassPlan &p_plan,
    int p_index
) const {
    return pool->restore_payload_of(native_slot(), p_plan, p_index);
}

RecoveryPlan NetwPredictSlotEngine::plan_of(
    const predict::WritePlan &p_plan
) const {
    return pool->recovery_of(native_slot(), p_plan);
}

Array NetwPredictSlotEngine::state_columns(const Dictionary &p_payload) const {
    return pool->state_columns_of(native_slot(), p_payload);
}

void NetwPredictSlotEngine::close_replayed_entry(int64_t p_index) {
    const Dictionary solve = pool->close_replayed_entry(
        native_slot(),
        p_index,
        witness_sample(),
        island_participant_ids()
    );
    if (!solve.is_empty()) {
        maybe_demote_for_breach(p_index, solve);
    }
}

bool NetwPredictSlotEngine::is_steppable() const {
    return pool->slot_is_steppable(native_slot());
}

void NetwPredictSlotEngine::joint_pass(const NetwPredictTiming &p_timing) {
    const int64_t present = p_timing.get_tick() - 1;
    if (handle->get_reconcile_mode() != NetwPredict::RECONCILE_JOINT
        || !role_drives_island(role()) || core_seated() == nullptr
        || present < 0) {
        return;
    }
    NETW_ZONE_NC("predict engine joint pass", colors::PREDICTION);
    const predict::JointPassPlan plan = pool_joint_pass(present);
    if (!plan.valid) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: the joint pass at tick %d has no plan",
            int(p_timing.get_tick())
        );
        return;
    }
    const LocalVector<NetwPredictSlotEngine *> members = joint_group();
    const int64_t floor_transition = plan.floor;
    if (plan.heal) {
        for (uint32_t at = 0; at < members.size(); ++at) {
            members[at]->joint_heal();
        }
    } else {
        run_joint_pass(plan, members, floor_transition);
    }
    release_lingering(floor_transition);
    for (uint32_t at = 0; at < members.size(); ++at) {
        const int64_t seat = members[at]->native_slot();
        pool->set_joint_basis(seat, -1);
        pool->set_joint_relay_floor(seat, -1);
        pool->set_joint_epoch_floor(seat, -1);
    }
}

LocalVector<NetwPredictSlotEngine *> NetwPredictSlotEngine::joint_group() {
    LocalVector<NetwPredictSlotEngine *> out;
    out.push_back(this);
    const int64_t slot = native_slot();
    const TypedArray<NetwEntity> simulated
        = pool->roster_list(slot, NetwPredictionEngine::ROSTER_SIMULATED);
    for (int64_t at = 0; at < simulated.size(); ++at) {
        append_joint_member(out, simulated[at]);
    }
    const TypedArray<NetwEntity> lingering
        = pool->roster_list(slot, NetwPredictionEngine::ROSTER_JOINT_LINGERING);
    for (int64_t at = 0; at < lingering.size(); ++at) {
        if (!simulated.has(lingering[at])) {
            append_joint_member(out, lingering[at]);
        }
    }
    std::stable_sort(
        out.ptr(),
        out.ptr() + out.size(),
        [](const NetwPredictSlotEngine *a, const NetwPredictSlotEngine *b) {
            return a->order_key() < b->order_key();
        }
    );
    return out;
}

void NetwPredictSlotEngine::append_joint_member(
    LocalVector<NetwPredictSlotEngine *> &r_out,
    const Ref<NetwEntity> &p_member
) {
    if (p_member.is_null()) {
        return;
    }
    NetwPredictSlotEngine *member = sibling(p_member);
    if (member == nullptr || member == this || !member->is_steppable()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: an island member cannot be re-run here"
        );
        return;
    }
    r_out.push_back(member);
}

NetwPredictSlotEngine::SteppedIsland NetwPredictSlotEngine::
    stepped_island() const {
    SteppedIsland out;
    NetwMultiplayer *host = core_seated();
    if (host == nullptr || schedule() != NetwPredict::SCHEDULE_STEPPED) {
        return out;
    }
    out.space = host->entity_space_of(seated_entity()).space;
    out.stepper = host->predict_get_stepper(out.space);
    return out;
}

void NetwPredictSlotEngine::step_island(
    const SteppedIsland &p_island,
    int64_t p_transition
) {
    if (p_island.stepper.is_null()) {
        return;
    }
    p_island.stepper->step(p_island.space, tick_delta());
    p_island.stepper->snapshot(p_island.space, p_transition);
}

void NetwPredictSlotEngine::run_joint_pass(
    const predict::JointPassPlan &p_plan,
    const LocalVector<NetwPredictSlotEngine *> &p_members,
    int64_t p_floor_transition
) {
    NETW_ZONE_NC("predict engine joint replay", colors::PREDICTION);
    HashMap<int64_t, NetwPredictSlotEngine *> by_slot;
    for (uint32_t at = 0; at < p_members.size(); ++at) {
        const int64_t seat = p_members[at]->native_slot();
        if (seat >= 0) {
            by_slot.insert(seat, p_members[at]);
        }
    }
    LocalVector<NetwPredictSlotEngine *> stepped;
    LocalVector<Dictionary> live;
    for (int at = 0; at < int(p_plan.restores.size()); ++at) {
        const int64_t restore_slot = p_plan.restores[uint32_t(at)].slot;
        NetwPredictSlotEngine **held = by_slot.getptr(restore_slot);
        if (held == nullptr) {
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: the plan restores slot %d nothing seats",
                int(restore_slot)
            );
            continue;
        }
        NetwPredictSlotEngine *member = *held;
        stepped.push_back(member);
        live.push_back(member->capture_input_raw());
        member->restore(
            member->restore_payload(p_plan, at),
            NetwPredictJournal::Operator::JOINT_REBASE,
            p_floor_transition,
            this
        );
    }
    if (stepped.is_empty()) {
        return;
    }
    LocalVector<Dictionary> before;
    LocalVector<int> ran;
    for (uint32_t at = 0; at < stepped.size(); ++at) {
        before.push_back(stepped[at]->capture());
        ran.push_back(0);
    }
    const SteppedIsland island = stepped_island();
    if (island.stepper.is_valid()) {
        island.stepper->restore(island.space, p_floor_transition);
    }
    int64_t open_transition = -1;
    for (int at = 0; at < int(p_plan.steps.size()); ++at) {
        const predict::JointStep &step = p_plan.steps[uint32_t(at)];
        NetwPredictSlotEngine **held = by_slot.getptr(step.slot);
        if (held == nullptr) {
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: the plan steps slot %d nothing seats",
                int(step.slot)
            );
            continue;
        }
        NetwPredictSlotEngine *member = *held;
        const int64_t transition = step.transition;
        if (open_transition >= 0 && transition != open_transition) {
            step_island(island, open_transition);
        }
        open_transition = transition;
        member->run(step.command, member->tick_delta(), transition, false);
        member->close_replayed_entry(transition);
        for (uint32_t seat = 0; seat < stepped.size(); ++seat) {
            if (stepped[seat] == member) {
                ran[seat] += 1;
            }
        }
    }
    if (open_transition >= 0) {
        step_island(island, open_transition);
    }
    const int64_t present = p_plan.present;
    for (uint32_t at = 0; at < stepped.size(); ++at) {
        stepped[at]->apply_input_raw(live[at]);
        pool->note_replay_depth(stepped[at]->native_slot(), ran[at]);
        stepped[at]->note_joint_writes(before[at]);
    }
    const int64_t depth = present - p_floor_transition;
    const Ref<NetwPredictStats> stats = handle->get_stats();
    Dictionary joint_depth
        = stats->get_dict_fact(NetwPredictStats::FACT_JOINT_DEPTH);
    joint_depth[depth] = int64_t(joint_depth.get(depth, 0)) + 1;
    stats->set_dict_fact(NetwPredictStats::FACT_JOINT_DEPTH, joint_depth);
}

void NetwPredictSlotEngine::release_lingering(int64_t p_floor_transition) {
    handle->get_stats()->set_int_fact(
        NetwPredictStats::FACT_LINGER_HELD,
        pool->release_lingering(native_slot(), p_floor_transition)
    );
}

void NetwPredictSlotEngine::joint_heal() {
    const Dictionary newest = transition_state_at(ledger_drive_frontier());
    if (newest.is_empty()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: a joint heal found no recorded state to snap to"
        );
        return;
    }
    restore(newest, NetwPredictJournal::Operator::JOINT_REBASE, -1);
}

void NetwPredictSlotEngine::note_joint_writes(const Dictionary &p_before) {
    const int64_t slot = native_slot();
    const Dictionary deltas = pool->write_deltas(slot, p_before, Dictionary());
    if (!deltas.is_empty()) {
        pool->ledger_note_writes(slot, deltas);
    }
}

int64_t NetwPredictSlotEngine::ledger_drive_frontier() const {
    return pool->drive_frontier(native_slot());
}

predict::JointPassPlan NetwPredictSlotEngine::pool_joint_pass(
    int64_t p_present
) {
    const int64_t slot = native_slot();
    if (slot < 0
        || pool->island_of(slot) == NetwPredictionEngine::ISLAND_NONE) {
        return predict::JointPassPlan();
    }
    return pool->joint_pass(slot, p_present);
}

void NetwPredictSlotEngine::host_local_author_tick(int64_t p_tick) {
    const int64_t slot = native_slot();
    const Dictionary input = capture_input_raw();
    const Ref<NetwTimeline> held = timeline();
    if (held.is_valid()) {
        held->record_input(p_tick, input);
    }
    pool->set_latest_input_tick(slot, p_tick);
    pool->set_frame_input(slot, input);
    const Ref<NetwPropertySetBinding> input_set = input_binding();
    if (input_set.is_valid()) {
        input_set->set_authored_tick(p_tick);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d authors with no input binding",
            int(p_tick)
        );
    }
    record_input_to_pool(p_tick, input);
}

void NetwPredictSlotEngine::host_local_frame_step(
    const NetwPredictTiming &p_timing
) {
    const int64_t slot = native_slot();
    if (!p_timing.get_simulating()
        || p_timing.get_tick() <= pool->last_frame_transition_tick_of(slot)) {
        charge_authoring_clamp();
        return;
    }
    pool->set_last_frame_transition_tick(slot, p_timing.get_tick());
    const Fold fold = drive_fold(p_timing.get_tick());
    const int64_t label = fold.label;
    const bool fresh = fold.fresh;
    record_drive(
        label,
        label,
        int(fold.kind),
        pool->frame_input_of(slot),
        p_timing.get_tick(),
        true,
        true
    );
    run(pool->frame_input_of(slot), p_timing.get_delta(), label, fresh);
    pool->set_ack_advanced(slot, fresh);
    if (!fresh) {
        return;
    }
    pool->set_last_driven_input_tick(slot, label);
    const Ref<NetwPropertySetBinding> state = state_binding();
    if (state.is_valid()) {
        state->set_authored_tick(label);
        state->set_reconcile_ack(label);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: label %d drove with no state binding",
            int(label)
        );
    }
}

void NetwPredictSlotEngine::host_local_step(double p_delta, int64_t p_tick) {
    pool->host_local_step(native_slot(), p_delta, p_tick);
}

void NetwPredictSlotEngine::on_input_frame(const Dictionary &p_header) {
    Array samples = p_header.get(key_samples(), Array());
    if (samples.is_empty()) {
        const int64_t tick = int64_t(p_header.get(key_tick(), -1));
        if (tick >= 0) {
            Dictionary one;
            one[key_tick()] = tick;
            one[key_payload()] = p_header.get(key_payload(), Dictionary());
            samples.push_back(one);
        }
    }
    const Ref<NetwTimeline> held = timeline();
    if (held.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: an input frame arrived with no timeline"
        );
        return;
    }
    int64_t oldest = -1;
    for (int64_t at = 0; at < samples.size(); ++at) {
        const Dictionary sample = samples[at];
        const int64_t stick = int64_t(sample.get(key_tick(), -1));
        if (stick < 0) {
            continue;
        }
        held->record_input(stick, sample.get(key_payload(), Dictionary()));
        oldest = oldest < 0 ? stick : std::min(oldest, stick);
    }
    const int64_t slot = native_slot();
    if (pool->next_input_tick_of(slot) < 0 && oldest >= 0) {
        pool->set_next_input_tick(slot, oldest);
    }
}

void NetwPredictSlotEngine::consume_step(
    double p_delta,
    int64_t p_server_tick
) {
    NETW_ZONE_NC("predict engine consume step", colors::PREDICTION);
    const int64_t slot = native_slot();
    const int64_t previous_ack = ack();
    const Ref<NetwPredictStats> stats = handle->get_stats();
    if (pool->next_input_tick_of(slot) >= 0) {
        resync_if_stranded();
        const int depth = int(queued_span());
        const int buffer = std::max(0, handle->get_consume_buffer_ticks());
        const int action = consume_action(depth, buffer);
        pool->report_consume(slot, depth, buffer, action);
        switch (action) {
            case NetwPredict::CONSUME_ACTION_REPLAY:
                consume_one(p_delta);
                break;
            case NetwPredict::CONSUME_ACTION_HOLD:
                stats->set_int_fact(
                    NetwPredictStats::FACT_HELD,
                    stats->get_int_fact(NetwPredictStats::FACT_HELD) + 1
                );
                mark_idle_drive(ack(), NetwPredict::DRIVE_KIND_HOLD);
                NETW_TRACE(
                    sys::PREDICTION,
                    "predict engine: tick %d holds a queue of %d",
                    int(p_server_tick),
                    depth
                );
                break;
            case NetwPredict::CONSUME_ACTION_STARVED:
                stats->set_int_fact(
                    NetwPredictStats::FACT_STARVED,
                    stats->get_int_fact(NetwPredictStats::FACT_STARVED) + 1
                );
                mark_idle_drive(ack(), NetwPredict::DRIVE_KIND_STARVED);
                NETW_TRACE(
                    sys::PREDICTION,
                    "predict engine: tick %d starved on an empty queue",
                    int(p_server_tick)
                );
                break;
            default:
                break;
        }
    }
    pool->set_ack_advanced(slot, ack() != previous_ack);
    const Ref<NetwPropertySetBinding> state = state_binding();
    if (state.is_valid()) {
        state->set_authored_tick(p_server_tick);
        state->set_reconcile_ack(ack_advanced() ? ack() : -1);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d consumed with no state binding",
            int(p_server_tick)
        );
    }
    const Ref<NetwTimeline> held = timeline();
    handle->set_ack_age_ticks(
        held.is_valid()
            ? int(std::max(int64_t(0), held->newest_input_tick() - ack()))
            : 0
    );
    send_ack_frame();
}

void NetwPredictSlotEngine::consume_frame_step(
    const NetwPredictTiming &p_timing
) {
    NETW_ZONE_NC("predict engine consume frame step", colors::PREDICTION);
    const int64_t slot = native_slot();
    const int64_t previous_ack = ack();
    pool->set_last_replayed_fresh(slot, false);
    const int depth = replay_depth();
    const Ref<NetwPredictStats> stats = handle->get_stats();
    const int64_t drove_before
        = stats->get_int_fact(NetwPredictStats::FACT_DRIVE_SEQ);
    record_consume_cadence(depth);
    const int buffer = std::max(0, handle->get_replay_buffer_depth());
    const int action = consume_action(depth, buffer);
    pool->report_consume(slot, depth, buffer, action);
    switch (action) {
        case NetwPredict::CONSUME_ACTION_REPLAY:
            resync_tape_if_stranded(depth, buffer);
            replay_tape_entry(p_timing.get_delta(), p_timing.get_tick());
            break;
        case NetwPredict::CONSUME_ACTION_HOLD:
            stats->set_int_fact(
                NetwPredictStats::FACT_HELD,
                stats->get_int_fact(NetwPredictStats::FACT_HELD) + 1
            );
            mark_idle_drive(
                pool->last_replayed_label_of(slot),
                NetwPredict::DRIVE_KIND_HOLD
            );
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: frame %d holds a tape depth of %d",
                int(p_timing.get_tick()),
                depth
            );
            break;
        case NetwPredict::CONSUME_ACTION_STARVED:
            stats->set_int_fact(
                NetwPredictStats::FACT_STARVED,
                stats->get_int_fact(NetwPredictStats::FACT_STARVED) + 1
            );
            mark_idle_drive(
                pool->last_replayed_label_of(slot),
                NetwPredict::DRIVE_KIND_STARVED
            );
            NETW_TRACE(
                sys::PREDICTION,
                "predict engine: frame %d starved on an empty tape",
                int(p_timing.get_tick())
            );
            break;
        default:
            break;
    }
    bump_consume_shape(
        stats->get_int_fact(NetwPredictStats::FACT_DRIVE_SEQ) - drove_before
    );
    pool->set_ack_advanced(slot, ack() != previous_ack);
    const Ref<NetwPropertySetBinding> state = state_binding();
    if (state.is_valid()) {
        state->set_authored_tick(p_timing.get_tick());
        state->set_reconcile_ack(ack_advanced() ? ack() : -1);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: frame %d consumed with no state binding",
            int(p_timing.get_tick())
        );
    }
    refresh_tape_diagnostics();
    handle->set_ack_age_ticks(replay_depth());
    send_ack_frame();
}

void NetwPredictSlotEngine::record_consume_cadence(int p_depth) {
    const int64_t slot = native_slot();
    const int arrivals
        = std::min(pool->arrivals_this_frame_of(slot), ARRIVAL_BUCKET_MAX);
    pool->set_arrivals_this_frame(slot, 0);
    const Ref<NetwPredictStats> stats = handle->get_stats();
    PackedInt32Array arrival_buckets
        = stats->get_buckets_fact(NetwPredictStats::FACT_ARRIVALS);
    if (arrivals >= 0 && arrivals < arrival_buckets.size()) {
        arrival_buckets.set(arrivals, arrival_buckets[arrivals] + 1);
        stats->set_buckets_fact(
            NetwPredictStats::FACT_ARRIVALS,
            arrival_buckets
        );
    }
    PackedInt32Array depth_buckets
        = stats->get_buckets_fact(NetwPredictStats::FACT_REPLAY_DEPTH);
    const int bucket = std::min(p_depth, REPLAY_DEPTH_BUCKET_MAX);
    if (bucket >= 0 && bucket < depth_buckets.size()) {
        depth_buckets.set(bucket, depth_buckets[bucket] + 1);
        stats->set_buckets_fact(
            NetwPredictStats::FACT_REPLAY_DEPTH,
            depth_buckets
        );
    }
}

void NetwPredictSlotEngine::bump_consume_shape(int64_t p_consumed) {
    const Ref<NetwPredictStats> stats = handle->get_stats();
    const String key
        = String::num_int64(p_consumed) + String(",")
        + String::num_int64(
              stats->get_int_fact(NetwPredictStats::FACT_QUANTUM_STEPS)
        );
    Dictionary shape
        = stats->get_dict_fact(NetwPredictStats::FACT_CONSUME_SHAPE);
    shape[key] = int64_t(shape.get(key, 0)) + 1;
    stats->set_dict_fact(NetwPredictStats::FACT_CONSUME_SHAPE, shape);
}

void NetwPredictSlotEngine::replay_tape_entry(
    double p_delta,
    int64_t p_timing_tick
) {
    pool->replay_tape_entry(native_slot(), p_delta, p_timing_tick);
}

void NetwPredictSlotEngine::resync_tape_if_stranded(int p_depth, int p_buffer) {
    pool->resync_tape_if_stranded(
        native_slot(),
        p_depth,
        p_buffer,
        handle->get_max_consume_lag_ticks()
    );
}

int NetwPredictSlotEngine::replay_depth() const {
    if (core_seated() == nullptr) {
        return 0;
    }
    const int64_t slot = native_slot();
    return pool->command_depth_from(slot, pool->replay_cursor_of(slot));
}

void NetwPredictSlotEngine::resync_if_stranded() {
    pool->resync_input_if_stranded(
        native_slot(),
        handle->get_max_consume_lag_ticks(),
        handle->get_consume_buffer_ticks()
    );
}

int64_t NetwPredictSlotEngine::queued_span() const {
    return pool->queued_span(native_slot());
}

void NetwPredictSlotEngine::consume_one(double p_delta) {
    pool->consume_one(native_slot(), p_delta);
}

Dictionary NetwPredictSlotEngine::record_drive(
    int64_t p_transition,
    int64_t p_label,
    int p_kind,
    const Dictionary &p_input,
    int64_t p_drive_tick,
    bool p_caller_selected,
    bool p_input_recorded,
    bool p_authoring
) {
    return pool->record_drive(
        native_slot(),
        p_transition,
        p_label,
        p_kind,
        p_input,
        p_drive_tick,
        p_caller_selected,
        p_input_recorded,
        p_authoring
    );
}

void NetwPredictSlotEngine::mark_idle_drive(int64_t p_label, int p_kind) {
    if (core_seated() != nullptr) {
        pool->record_idle_drive(native_slot(), p_label, p_kind);
    }
}

void NetwPredictSlotEngine::adopt_tape_position(int64_t p_transition) {
    const int64_t slot = native_slot();
    pool->set_last_driven_entry_index(slot, p_transition);
    const Ref<NetwPredictStats> stats = handle->get_stats();
    stats->set_int_fact(
        NetwPredictStats::FACT_TAPE_EPOCH,
        pool->tape_epoch_of(slot)
    );
    stats->set_int_fact(NetwPredictStats::FACT_TAPE_INDEX, p_transition);
    pool->set_next_tape_entry_index(slot, p_transition + 1);
}

void NetwPredictSlotEngine::author_command_entry(
    int64_t p_label,
    bool p_fresh
) {
    const int64_t slot = native_slot();
    if (core_seated() != nullptr) {
        pool->tape_author(slot, p_label, p_fresh);
    }
    adopt_tape_position(pool->next_tape_entry_index_of(slot));
}

void NetwPredictSlotEngine::refresh_tape_diagnostics() {
    pool->refresh_tape_diagnostics(native_slot());
}

void NetwPredictSlotEngine::run(
    const Dictionary &p_input,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    const int64_t slot = native_slot();
    if (core_seated() != nullptr && pool->owner_bound(slot)) {
        pool->run_step(slot, p_input, p_delta, p_tick, p_fresh);
        return;
    }
    apply_input_raw(p_input);
    const Callable step = handle->get_simulate();
    if (step.is_valid()) {
        step.call(p_delta, p_tick, p_fresh);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: tick %d has no step to run",
            int(p_tick)
        );
    }
}

void NetwPredictSlotEngine::push_simulate() {
    if (core_seated() != nullptr) {
        pool->set_simulate(native_slot(), handle->get_simulate());
    }
}

Dictionary NetwPredictSlotEngine::capture() const {
    return pool->capture_canonical_state(native_slot());
}

Dictionary NetwPredictSlotEngine::capture_input_raw() const {
    return pool->capture_input_raw(native_slot());
}

void NetwPredictSlotEngine::apply_input_raw(const Dictionary &p_payload) {
    pool->apply_input_raw(native_slot(), p_payload);
}

int64_t NetwPredictSlotEngine::state_fingerprint(
    const Dictionary &p_payload
) const {
    return pool->state_fingerprint(native_slot(), p_payload);
}

void NetwPredictSlotEngine::close_journal_row(
    int64_t p_transition,
    const Dictionary &p_state
) {
    NETW_ZONE_NC("predict close journal row", colors::PREDICTION);
    const Dictionary solve = pool->seal_transition(
        native_slot(),
        p_transition,
        p_state,
        witness_sample(),
        island_participant_ids()
    );
    if (solve.is_empty()) {
        return;
    }
    maybe_demote_for_breach(p_transition, solve);
}

Dictionary NetwPredictSlotEngine::witness_sample() const {
    return pool->witness_sample(native_slot(), handle);
}

PackedStringArray NetwPredictSlotEngine::island_participant_ids() const {
    return pool->live_participant_ids(native_slot(), handle->get_island());
}

void NetwPredictSlotEngine::maybe_demote_for_breach(
    int64_t p_transition,
    const Dictionary &p_solve
) {
    pool->demote_for_breach(
        native_slot(),
        p_transition,
        bool(p_solve.get(key_breach(), false)),
        int(p_solve.get(key_witness_fp(), 0)),
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_command_frame),
        callable_mp(handle.ptr(), &NetwPredictionHandle::seat_fallback)
    );
}

void NetwPredictSlotEngine::restore(
    const Dictionary &p_payload,
    int p_operator,
    int64_t p_basis,
    NetwPredictSlotEngine *p_provenance_owner,
    bool p_evidence_free,
    bool p_pool_planned
) {
    NetwPredictSlotEngine *episode_owner
        = p_provenance_owner != nullptr ? p_provenance_owner : this;
    if (!pool->apply_restore(
            native_slot(),
            p_payload,
            p_operator,
            p_basis,
            episode_owner->native_slot(),
            entity.is_valid() ? entity->get_entity_id() : StringName(),
            handle->get_ack_age_ticks(),
            handle->get_divergence_epsilon(),
            p_evidence_free,
            p_pool_planned
        )) {
        NETW_ERROR(sys::PREDICTION, "prediction restore effect is invalid");
        return;
    }
    if (p_operator != NetwPredictJournal::Operator::NONE && !p_pool_planned) {
        episode_owner->sync_episode();
    }
}

void NetwPredictSlotEngine::register_with_loop() {
    NetwMultiplayer *seated = core_seated();
    NetwPredictRunner *runner
        = seated != nullptr ? seated->predict_runner_seated() : nullptr;
    if (runner != nullptr) {
        runner->register_engine(this);
        pool->set_registered(native_slot(), true);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "predict engine: no runner seats this engine"
        );
    }
}

void NetwPredictSlotEngine::unregister_from_loop() {
    NetwMultiplayer *seated = core_seated();
    NetwPredictRunner *runner
        = seated != nullptr ? seated->predict_runner_seated() : nullptr;
    const int64_t slot = native_slot();
    if (runner != nullptr) {
        runner->unregister_engine(this);
    }
    pool->set_registered(slot, false);
}

} // namespace netw
