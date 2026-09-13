#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"

#include "netw/api/participant.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_server.hpp"
#include "godot/project_settings.hpp"
#include "godot/rendering_server.hpp"
#include "godot/resource.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;
using namespace netw;

namespace netw {

Error NetwMultiplayer::predict_admit_frame_default(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    const Ref<NetwEntity> entity
        = Object::cast_to<NetwEntity>(wrapper_for_route(p_route).ptr());
    return Error(
        prediction_core::admit_frame(
            int(p_channel),
            int(p_sender),
            entity.is_valid() ? int(entity->get_controller()) : 0,
            is_server(),
            p_payload.is_empty(),
            int(entity_frame_verdict(p_route))
        )
    );
}

Error NetwMultiplayer::predict_admit_frame(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("predict admit frame", colors::PREDICTION);
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _predict_admit_frame,
            p_sender,
            p_route,
            p_channel,
            p_payload,
            answered
        )) {
        return answered;
    }
    return predict_admit_frame_default(p_sender, p_route, p_channel, p_payload);
}

void NetwMultiplayer::predict_relay_subscribe(
    const RID &p_entity,
    bool p_subscribed
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return;
    }
    if (is_host()) {
        relay_subscribe(wrapper, get_unique_id(), p_subscribed);
        return;
    }
    const int64_t route = liveness_route_of(wrapper.ptr());
    if (route <= 0) {
        return;
    }
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        route,
        wire::builtin_channel("PREDICT_RELAY_REQUEST"),
        predict::RelayBook::request_bytes(p_subscribed),
        true,
        0,
        String(),
        false
    );
}

int64_t NetwMultiplayer::arm_rewind_timeline(const Ref<NetwEntity> &p_entity) {
    const int64_t slot = lagcomp_core.timeline_slot_of(p_entity);
    if (slot < 0) {
        return -1;
    }
    const Ref<NetwPropertySetBinding> state = p_entity->get_state_binding();
    if (state.is_null() || state->get_set().is_null()) {
        return -1;
    }
    Node *node = state->node();
    if (node == nullptr) {
        return -1;
    }
    lagcomp_core.timeline_bind_owner(slot, node);
    Array keys;
    const TypedArray<NetwPropertySetColumn> columns
        = state->get_set()->get_columns();
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        keys.push_back(column->get_key());
    }
    lagcomp_core.timeline_declare(slot, keys);
    return slot;
}

void NetwMultiplayer::lagcomp_rewind(
    const TypedArray<RID> &p_entities,
    int64_t p_tick,
    const Callable &p_body
) {
    PackedInt64Array slots;
    for (int at = 0; at < p_entities.size(); at++) {
        const Ref<NetwEntity> wrapper = entity_get_view(p_entities[at]);
        if (wrapper.is_null()) {
            continue;
        }
        const int64_t slot = arm_rewind_timeline(wrapper);
        if (slot >= 0) {
            slots.push_back(slot);
        }
    }
    lagcomp_core.rewind(slots, p_tick, p_body);
}

Ref<DictionaryRecord> NetwMultiplayer::lagcomp_sample(
    const RID &p_entity,
    int64_t p_tick
) const {
    return lagcomp_sample_of(entity_get_view(p_entity), p_tick);
}

Ref<DictionaryRecord> NetwMultiplayer::lagcomp_sample_of(
    const Ref<RefCounted> &p_entity,
    int64_t p_tick
) const {
    Ref<DictionaryRecord> sampled;
    sampled.instantiate();
    if (p_entity.is_null()) {
        return sampled;
    }
    sampled->set_data(
        lagcomp_core.timeline_sample_entity(p_entity, p_tick).duplicate()
    );
    return sampled;
}

bool NetwMultiplayer::predict_tap_open() {
    if (predict_tap_disarmed) {
        return false;
    }
    if (predict_tap.ready()) {
        return true;
    }
    const String dir = predict::Tap::armed_dir();
    if (dir.is_empty()) {
        predict_tap_disarmed = true;
        return false;
    }
    predict_tap.open(dir);
    return true;
}

void NetwMultiplayer::predict_tap_drain(
    const StringName &p_entity_id,
    int64_t p_slot,
    const Dictionary &p_stats
) {
    if (predict_tap.ready()) {
        predict_tap.drain(p_entity_id, prediction_engine, p_slot, p_stats);
    }
}

void NetwMultiplayer::predict_tap_pump() {
    NETW_ZONE_NC("NetwPredict tap pump", colors::PREDICTION);
    if (!predict_tap_open()) {
        return;
    }
    if (predict_tap_every <= 0) {
        const int64_t authored = int64_t(
            OS::get_singleton()
                ->get_environment("NETW_PREDICT_TAP_EVERY")
                .to_int()
        );
        predict_tap_every = authored > 1 ? authored : 1;
    }
    predict_tap_frame += 1;
    if (predict_tap_frame % predict_tap_every != 0) {
        NETW_PLOT(profile::names::PREDICT_TAP_DRAINED, 0.0);
        return;
    }
    const TypedArray<Object> entities = predict_engine_entities();
    int64_t drained = 0;
    for (int at = 0; at < entities.size(); at++) {
        NetwEntity *entity = Object::cast_to<NetwEntity>(entities[at]);
        if (entity == nullptr) {
            continue;
        }
        const Ref<NetwPredictionHandle> handle = entity->get_prediction();
        if (handle.is_null() || handle->get_stats().is_null()) {
            continue;
        }
        predict_tap_drain(
            entity->get_entity_id(),
            native_prediction_slot(entity),
            handle->get_stats()->to_dictionary()
        );
        drained += 1;
    }
    NETW_PLOT(profile::names::PREDICT_TAP_DRAINED, double(drained));
    NETW_ZONE_VALUE(uint64_t(entities.size() - drained));
}

void NetwMultiplayer::predict_flush_tap() {
    if (predict_tap.ready()) {
        predict_tap.flush();
    }
}

Dictionary NetwMultiplayer::predict_get_tap_cost() const {
    return predict_tap.ready() ? predict_tap.cost() : Dictionary();
}

void NetwMultiplayer::predict_tap_close() {
    if (predict_tap.ready()) {
        predict_tap.close();
    }
    predict_tap_every = 0;
}

namespace {

void space_set_active(
    const NetwMultiplayer::EntitySpace &p_held,
    bool p_active
) {
    if (!p_held.space.is_valid()) {
        return;
    }
    if (p_held.dimension == 3) {
        PhysicsServer3D::get_singleton()->space_set_active(
            p_held.space,
            p_active
        );
        return;
    }
    if (p_held.dimension == 2) {
        PhysicsServer2D::get_singleton()->space_set_active(
            p_held.space,
            p_active
        );
    }
}

} // namespace

void NetwMultiplayer::simulation_gate_set(const RID &p_entity, bool p_wanted) {
    if (!p_entity.is_valid()) {
        return;
    }
    int64_t index = -1;
    for (uint32_t i = 0; i < gated_bodies.size(); ++i) {
        if (gated_bodies[i].entity == p_entity) {
            index = int64_t(i);
            break;
        }
    }
    if (p_wanted == (index >= 0)) {
        return;
    }
    ClockEngine &clock = clock_engine();
    if (p_wanted) {
        GatedBody row;
        row.entity = p_entity;
        row.held = entity_space_of(entity_get_view(p_entity));
        gated_bodies.push_back(row);
        clock.arm_gate();
        return;
    }
    const GatedBody &releasing = gated_bodies[uint32_t(index)];
    if (!predict_space_is_stepped(releasing.held.space)) {
        space_set_active(releasing.held, true);
    }
    gated_bodies.remove_at(uint32_t(index));
    clock.release_gate();
}

void NetwMultiplayer::simulation_gate_apply() {
    if (gated_bodies.is_empty()) {
        return;
    }
    const bool active = clock_engine().is_simulating();
    uint32_t i = 0;
    while (i < gated_bodies.size()) {
        GatedBody &row = gated_bodies[i];
        const Ref<NetwEntity> wrapper = entity_get_view(row.entity);
        if (wrapper.is_null() || wrapper->get_owner() == nullptr) {
            gated_bodies.remove_at(i);
            continue;
        }
        const EntitySpace resolved = entity_space_of(wrapper);
        if (resolved.space != row.held.space) {
            if (!predict_space_is_stepped(row.held.space)) {
                space_set_active(row.held, true);
            }
            row.held = resolved;
        }
        space_set_active(
            row.held,
            active && !predict_space_is_stepped(row.held.space)
        );
        ++i;
    }
    if (gated_bodies.is_empty()) {
        while (clock_engine().is_gated()) {
            clock_engine().release_gate();
        }
    }
}

int64_t NetwMultiplayer::simulation_gate_count() const {
    return int64_t(gated_bodies.size());
}

void NetwMultiplayer::physics_frame_advance() {
    if (clock_engine().is_simulating()) {
        physics_frame += 1;
    }
}

NetwPredictTiming NetwMultiplayer::tick_timing(
    int64_t p_tick,
    double p_delta
) const {
    return NetwPredictTiming::of(
        p_tick,
        p_delta,
        clock_engine().ticktime(),
        physics_frame,
        declared_quantum()
    );
}

Ref<NetwPredictFold> NetwMultiplayer::predict_drive(
    int64_t p_latest_input_tick,
    int64_t p_last_driven_input_tick,
    int64_t p_frame_tick
) {
    Ref<NetwPredictFold> answered;
    if (GDVIRTUAL_CALL(
            _predict_drive,
            p_latest_input_tick,
            p_last_driven_input_tick,
            p_frame_tick,
            answered
        )) {
        return answered;
    }
    return predict_drive_default(
        p_latest_input_tick,
        p_last_driven_input_tick,
        p_frame_tick
    );
}

Ref<NetwPredictFold> NetwMultiplayer::predict_drive_default(
    int64_t p_latest_input_tick,
    int64_t p_last_driven_input_tick,
    int64_t p_frame_tick
) {
    return prediction_core::predict_fold(
        p_latest_input_tick,
        p_last_driven_input_tick,
        p_frame_tick
    );
}

int NetwMultiplayer::predict_consume(int p_depth, int p_buffer) {
    int answered = 0;
    if (GDVIRTUAL_CALL(_predict_consume, p_depth, p_buffer, answered)) {
        return answered;
    }
    return predict_consume_default(p_depth, p_buffer);
}

int NetwMultiplayer::predict_consume_default(int p_depth, int p_buffer) {
    return prediction_core::consume_action(p_depth, p_buffer);
}

Ref<NetwPredictJudgement> NetwMultiplayer::predict_evaluate(
    NetwPredictJournal::Domain p_domain,
    NetwPredict::ExactVerdict p_verdict,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    const Dictionary &p_wiring,
    const Dictionary &p_field_sink
) {
    Ref<NetwPredictJudgement> answered;
    if (GDVIRTUAL_CALL(
            _predict_evaluate,
            p_domain,
            p_verdict,
            p_predicted,
            p_payload,
            p_wiring,
            p_field_sink,
            answered
        )) {
        return answered;
    }
    return predict_evaluate_default(
        p_domain,
        p_verdict,
        p_predicted,
        p_payload,
        p_wiring,
        p_field_sink
    );
}

Ref<NetwPredictJudgement> NetwMultiplayer::predict_evaluate_default(
    NetwPredictJournal::Domain p_domain,
    NetwPredict::ExactVerdict p_verdict,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    const Dictionary &p_wiring,
    const Dictionary &p_field_sink
) {
    return prediction_core::evaluate(
        p_domain,
        p_verdict,
        p_predicted,
        p_payload,
        p_wiring,
        p_field_sink
    );
}

Ref<NetwPredictRecovery> NetwMultiplayer::predict_recover(
    const Dictionary &p_payload,
    NetwPredict::RecoveryPolicy p_policy,
    NetwPredict::CorrectionMode p_correction,
    NetwPredict::RestoreMode p_snap_restore,
    const Dictionary &p_projection,
    const Dictionary &p_current,
    const Dictionary &p_pose_errors,
    const Dictionary &p_wiring,
    const Dictionary &p_verdict,
    double p_tick_delta
) {
    Ref<NetwPredictRecovery> answered;
    if (GDVIRTUAL_CALL(
            _predict_recover,
            p_payload,
            p_policy,
            p_correction,
            p_snap_restore,
            p_projection,
            p_current,
            p_pose_errors,
            p_wiring,
            p_verdict,
            p_tick_delta,
            answered
        )) {
        return answered;
    }
    return predict_recover_default(
        p_payload,
        p_policy,
        p_correction,
        p_snap_restore,
        p_projection,
        p_current,
        p_pose_errors,
        p_wiring,
        p_verdict,
        p_tick_delta
    );
}

Ref<NetwPredictRecovery> NetwMultiplayer::predict_recover_default(
    const Dictionary &p_payload,
    NetwPredict::RecoveryPolicy p_policy,
    NetwPredict::CorrectionMode p_correction,
    NetwPredict::RestoreMode p_snap_restore,
    const Dictionary &p_projection,
    const Dictionary &p_current,
    const Dictionary &p_pose_errors,
    const Dictionary &p_wiring,
    const Dictionary &p_verdict,
    double p_tick_delta
) {
    return prediction_core::recover(
        p_payload,
        p_policy,
        p_correction,
        p_snap_restore,
        p_projection,
        p_current,
        p_pose_errors,
        p_wiring,
        p_verdict,
        p_tick_delta
    );
}

void NetwMultiplayer::predict_stepper_install(
    const RID &p_space,
    const Ref<NetwPhysicsStepper> &p_stepper
) {
    if (p_stepper.is_null() || !p_stepper->can_step()) {
        const SteppedSpace *held = space_steppers.getptr(p_space.get_id());
        if (held != nullptr && held->inactive) {
            space_set_active({held->space, held->dimension}, true);
        }
        space_steppers.erase(p_space.get_id());
        predict_reresolve_space(p_space);
        return;
    }
    SteppedSpace row;
    row.space = p_space;
    row.stepper = p_stepper;
    space_steppers.insert(p_space.get_id(), row);
    predict_reresolve_space(p_space);
}

void NetwMultiplayer::predict_reresolve_space(const RID &p_space) {
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        const Ref<NetwEntity> seated = wrapper_for_id(row.key);
        if (seated.is_null() || entity_space_of(seated).space != p_space) {
            continue;
        }
        row.value->rewire();
    }
}

Ref<NetwPhysicsStepper> NetwMultiplayer::predict_get_stepper(
    const RID &p_space
) const {
    const SteppedSpace *held = space_steppers.getptr(p_space.get_id());
    return held != nullptr ? held->stepper : Ref<NetwPhysicsStepper>();
}

void NetwMultiplayer::predict_stepper_hold(
    const RID &p_space,
    int p_dimension
) {
    SteppedSpace *held = space_steppers.getptr(p_space.get_id());
    if (held == nullptr || held->inactive) {
        return;
    }
    held->dimension = p_dimension;
    held->inactive = true;
    space_set_active({held->space, held->dimension}, false);
}

bool NetwMultiplayer::predict_space_is_stepped(const RID &p_space) const {
    return space_steppers.has(p_space.get_id());
}

void NetwMultiplayer::predict_engine_install(
    const RID &p_entity,
    NetwPredictSlotEngine *p_engine
) {
    if (p_engine == nullptr || predict_engines.has(p_entity.get_id())) {
        return;
    }
    predict_engines.insert(p_entity.get_id(), p_engine);
}

NetwPredictSlotEngine *NetwMultiplayer::predict_engine_release(
    const RID &p_entity
) {
    NetwPredictSlotEngine *const *held
        = predict_engines.getptr(p_entity.get_id());
    if (held == nullptr) {
        return nullptr;
    }
    NetwPredictSlotEngine *engine = *held;
    predict_engines.erase(p_entity.get_id());
    return engine;
}

NetwPredictSlotEngine *NetwMultiplayer::predict_engine_for(
    const RID &p_entity
) const {
    NetwPredictSlotEngine *const *held
        = predict_engines.getptr(p_entity.get_id());
    return held != nullptr ? *held : nullptr;
}

bool NetwMultiplayer::predict_engine_seated(const RID &p_entity) const {
    return predict_engine_for(p_entity) != nullptr;
}

TypedArray<Object> NetwMultiplayer::predict_engine_entities() const {
    TypedArray<Object> out;
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        const Ref<NetwEntity> wrapper = wrapper_for_id(row.key);
        if (wrapper.is_valid()) {
            out.push_back(wrapper);
        }
    }
    return out;
}

NetwPredictRunner *NetwMultiplayer::predict_runner_seated() {
    return &predict_runner;
}

namespace {

const char *SIG_NODE_ADDED = "node_added";

} // namespace

void NetwMultiplayer::predict_history_record(
    int64_t p_tick,
    int p_schedule,
    bool p_include_unregistered
) {
    NETW_ZONE_NC("predict history record", colors::PREDICTION);
    if (!lagcomp_configured || !is_server()) {
        return;
    }
    const Dictionary timelines = lagcomp_core.timeline_entities();
    const Array carriers = timelines.keys();
    for (int64_t at = 0; at < carriers.size(); ++at) {
        const Ref<NetwEntity> entity
            = Object::cast_to<NetwEntity>(carriers[at]);
        if (entity.is_null()) {
            continue;
        }
        Node *owner = entity->get_owner();
        if (owner == nullptr) {
            continue;
        }
        if (owner->is_inside_tree() && !owner->can_process()) {
            continue;
        }
        const Ref<NetwPropertySetBinding> state = entity->get_state_binding();
        if (state.is_null()) {
            continue;
        }
        int64_t record_tick = p_tick;
        NetwPredictSlotEngine *engine
            = predict_engine_for(entity->get_rid_handle());
        if (engine != nullptr) {
            if (!engine->uses_schedule(p_schedule)) {
                continue;
            }
            record_tick = engine->history_record_tick(p_tick);
            if (record_tick < 0 && !engine->consumed_unslotted_transition()) {
                NETW_TRACE(
                    sys::PREDICTION,
                    "history: tick %d slots nothing the ack can key",
                    int(p_tick)
                );
                continue;
            }
        } else if (!p_include_unregistered) {
            continue;
        }
        const Dictionary payload
            = state->canonicalize_payload(state->snapshot_payload());
        if (record_tick >= 0) {
            const Ref<NetwTimeline> history
                = Object::cast_to<NetwTimeline>(timelines[entity]);
            if (history.is_valid()) {
                history->record_state(record_tick, payload);
            }
        }
        if (engine != nullptr) {
            engine->finalize_recorded_state(payload);
        }
    }
}

bool NetwMultiplayer::lagcomp_is_configured() const {
    return lagcomp_configured;
}

void NetwMultiplayer::set_lagcomp_configured(bool p_configured) {
    lagcomp_configured = p_configured;
}

Callable NetwMultiplayer::lagcomp_node_watcher() {
    return callable_mp(this, &NetwMultiplayer::observe_node_added);
}

void NetwMultiplayer::lagcomp_observe_mounted(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    observe_node_added(p_node);
    const int children = p_node->get_child_count();
    for (int at = 0; at < children; ++at) {
        lagcomp_observe_mounted(p_node->get_child(at));
    }
}

void NetwMultiplayer::lagcomp_arm() {
    if (lagcomp_configured) {
        return;
    }
    set_lagcomp_configured(true);
    SceneTree *tree = gd::scene_tree();
    const Callable watcher = lagcomp_node_watcher();
    if (tree != nullptr && !tree->is_connected(SIG_NODE_ADDED, watcher)) {
        tree->connect(SIG_NODE_ADDED, watcher);
        lagcomp_observe_mounted(tree->get_root());
    }
    wire_lagcomp_service();
}

void NetwMultiplayer::lagcomp_release() {
    set_lagcomp_configured(false);
    SceneTree *tree = gd::scene_tree();
    const Callable watcher = lagcomp_node_watcher();
    if (tree != nullptr && tree->is_connected(SIG_NODE_ADDED, watcher)) {
        tree->disconnect(SIG_NODE_ADDED, watcher);
    }
    predict_tap_close();
}

void NetwMultiplayer::wire_lagcomp_service() {
    if (!clock_engine().get_configured() || !lagcomp_configured) {
        return;
    }
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "the action and prediction carriers have no replication plane "
            "to land on"
        );
        return;
    }
    plane->register_channel(
        declared_channel("ACTION"),
        callable_mp(this, &NetwMultiplayer::action_receive_carrier),
        false
    );
    plane->register_channel(
        declared_channel("PREDICT_COMMAND"),
        callable_mp(this, &NetwMultiplayer::predict_receive_command_carrier),
        false
    );
    plane->register_channel(
        declared_channel("PREDICT_ACK"),
        callable_mp(this, &NetwMultiplayer::predict_receive_ack_carrier),
        false
    );
    plane->register_channel(
        declared_channel("PREDICT_RELAY"),
        callable_mp(this, &NetwMultiplayer::predict_receive_relay_carrier),
        false
    );
    plane->register_channel(
        declared_channel("PREDICT_RELAY_REQUEST"),
        callable_mp(
            this,
            &NetwMultiplayer::predict_receive_relay_request_carrier
        ),
        false
    );
}

void NetwMultiplayer::predict_receive_command_carrier(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NETW_ZONE_NC("predict command carrier", colors::PREDICTION);
    NetwPredictSlotEngine *engine = predict_engine_for(
        p_entity.is_valid() ? p_entity->get_rid_handle() : RID()
    );
    if (engine != nullptr) {
        engine->receive_command_frame(p_payload);
    }
    predict_relay_command_frame(p_entity, p_payload, p_sender);
}

void NetwMultiplayer::predict_receive_relay_carrier(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t
) {
    NETW_ZONE_NC("predict relay carrier", colors::PREDICTION);
    NetwPredictSlotEngine *engine = predict_engine_for(
        p_entity.is_valid() ? p_entity->get_rid_handle() : RID()
    );
    if (engine != nullptr) {
        engine->receive_relayed_command_frame(p_payload);
    }
}

void NetwMultiplayer::predict_receive_ack_carrier(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t
) {
    NETW_ZONE_NC("predict ack carrier", colors::PREDICTION);
    NetwPredictSlotEngine *engine = predict_engine_for(
        p_entity.is_valid() ? p_entity->get_rid_handle() : RID()
    );
    if (engine != nullptr) {
        engine->receive_ack_frame(p_payload);
    }
}

void NetwMultiplayer::predict_receive_relay_request_carrier(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    const int request = relay_request_of(p_payload);
    if (request < 0) {
        NETW_TRACE(
            sys::PREDICTION,
            "peer %d sent an unreadable relay request",
            int(p_sender)
        );
        return;
    }
    relay_subscribe(p_entity, p_sender, request == 1);
}

void NetwMultiplayer::predict_relay_command_frame(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_author
) {
    if (!is_server() || p_entity.is_null()) {
        return;
    }
    const RID handle = p_entity->get_rid_handle();
    const PackedInt64Array subscribers = relay_peers(int64_t(handle.get_id()));
    if (subscribers.is_empty()) {
        return;
    }
    const int64_t route = liveness_route_of(p_entity.ptr());
    if (route <= 0) {
        NETW_TRACE(
            sys::PREDICTION,
            "a relayed command found no live route for entity %d",
            int(handle.get_id())
        );
        return;
    }
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        return;
    }
    const int64_t channel = declared_channel("PREDICT_RELAY");
    for (int at = 0; at < subscribers.size(); at++) {
        const int64_t peer = subscribers[at];
        if (peer == p_author) {
            continue;
        }
        if (!interest_admits(handle, peer)) {
            relay_subscribe(p_entity, peer, false);
            continue;
        }
        plane->send_to(
            peer,
            route,
            channel,
            p_payload,
            false,
            0,
            String(),
            false
        );
    }
}

void NetwMultiplayer::action_receive_carrier(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!lagcomp_configured || !is_server() || p_entity.is_null()) {
        return;
    }
    session::ActionRequest body;
    if (!session::frame_read(p_payload, body)) {
        NETW_TRACE(
            sys::PREDICTION,
            "peer %d sent an action carrier that did not decode whole",
            int(p_sender)
        );
        return;
    }
    submit_action(
        p_entity->get_route(),
        body.method,
        body.view_tick,
        gd::bytes_to_var(body.data),
        body.key,
        int(body.timing),
        p_sender
    );
}

void NetwMultiplayer::lagcomp_deny_action(
    int64_t p_requester,
    const StringName &p_key
) {
    const bool transport = lagcomp_configured
        && NETW_API_VIRTUAL(get_multiplayer_peer)().is_valid();
    const int64_t local_peer
        = transport ? int64_t(NETW_API_VIRTUAL(get_unique_id)()) : 0;
    const bool remote = transport && p_requester != 0
        && p_requester != local_peer
        && NETW_API_VIRTUAL(get_peer_ids)().has(int32_t(p_requester));
    if (remote) {
        NETW_TRACE(
            sys::PREDICTION,
            "action %s refused, telling peer %d to revert",
            String(p_key).utf8().get_data(),
            int(p_requester)
        );
        send_to(
            p_requester,
            0,
            wire::builtin_channel("LAGCOMP_DENY"),
            session::frame_write(session::DenyKey{p_key}),
            true,
            0,
            String(),
            false
        );
        return;
    }
    NETW_TRACE(
        sys::PREDICTION,
        "action %s refused locally, discarding its effect",
        String(p_key).utf8().get_data()
    );
    lagcomp_effect_discard(p_key);
}

void NetwMultiplayer::predict_history_record_tick(int64_t p_tick) {
    predict_history_record(p_tick, NetwPredict::SCHEDULE_TICK, true);
}

void NetwMultiplayer::tick_step(double p_delta, int64_t p_tick) {
    NETW_ZONE_NC("session tick step", colors::PREDICTION);
    if (!lagcomp_configured) {
        return;
    }
    drain_pending_actions(p_tick);
    predict_runner.tick_step(tick_timing(p_tick, p_delta));
    predict_history_record_tick(p_tick);
}

void NetwMultiplayer::before_frame_step() {
    NETW_ZONE_NC("session before frame step", colors::PREDICTION);
    if (!lagcomp_configured) {
        return;
    }
    physics_frame_advance();
    predict_runner.before_frame_step();
    predict_history_record_frame(clock_engine().get_tick());
    predict_tap_pump();
}

void NetwMultiplayer::frame_step() {
    NETW_ZONE_NC("session frame step", colors::PREDICTION);
    if (!lagcomp_configured) {
        return;
    }
    predict_runner.frame_step(frame_timing());
    simulation_gate_apply();
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "the frame closed with no replication plane to flush"
        );
        return;
    }
    plane->on_frame_end();
}

void NetwMultiplayer::predict_history_record_frame(int64_t p_tick) {
    predict_history_record(p_tick, NetwPredict::SCHEDULE_FRAME, false);
}

namespace {

enum PredictCensusFact {
    CENSUS_CORRECTIONS,
    CENSUS_MAX_REPLAY_DEPTH,
    CENSUS_CONSUMED,
    CENSUS_MISSING,
    CENSUS_FOLDED,
    CENSUS_JOINT_PASSES,
    CENSUS_JOINT_MEMBERS,
    CENSUS_CELLS_RELAYED,
    CENSUS_CELLS_SUBSTITUTED,
    CENSUS_HEAL_SNAPS,
    CENSUS_LINGER_HELD,
    CENSUS_COUNT,
};

const char *const CENSUS_NAMES[CENSUS_COUNT] = {
    "corrections",
    "max_replay_depth",
    "consumed",
    "missing",
    "folded",
    "joint_passes",
    "joint_members",
    "cells_relayed",
    "cells_substituted",
    "heal_snaps",
    "linger_held",
};

const int *predict_census_columns() {
    static int seated[CENSUS_COUNT];
    static bool resolved = false;
    if (resolved) {
        return seated;
    }
    const NetwPredictStats::Fact *facts = NetwPredictStats::facts();
    for (int at = 0; at < CENSUS_COUNT; ++at) {
        seated[at] = -1;
        for (int row = 0; row < NetwPredictStats::fact_count(); ++row) {
            if (String(facts[row].name) == String(CENSUS_NAMES[at])) {
                seated[at] = row;
                break;
            }
        }
    }
    resolved = true;
    return seated;
}

} // namespace

TypedArray<Object> NetwMultiplayer::predict_stepped_entities() const {
    TypedArray<Object> out;
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        const Ref<NetwEntity> wrapper = wrapper_for_id(row.key);
        if (wrapper.is_null()) {
            continue;
        }
        const int64_t slot = prediction_engine.slot_of(wrapper);
        if (slot >= 0 && prediction_engine.registered_of(slot)) {
            out.push_back(wrapper);
        }
    }
    return out;
}

Dictionary NetwMultiplayer::predict_metrics() const {
    const int *column = predict_census_columns();
    const TypedArray<Object> stepped = predict_stepped_entities();
    int64_t total[CENSUS_COUNT] = {0};
    for (int at = 0; at < stepped.size(); ++at) {
        const NetwEntity *wrapper = Object::cast_to<NetwEntity>(stepped[at]);
        if (wrapper == nullptr) {
            continue;
        }
        const Ref<NetwPredictionHandle> handle = wrapper->get_prediction();
        if (handle.is_null()) {
            continue;
        }
        const Ref<NetwPredictStats> counters = handle->get_stats();
        if (counters.is_null()) {
            continue;
        }
        for (int fact = 0; fact < CENSUS_COUNT; ++fact) {
            const int64_t held = counters->get_int_fact(column[fact]);
            if (fact == CENSUS_MAX_REPLAY_DEPTH
                || fact == CENSUS_JOINT_MEMBERS) {
                total[fact] = held > total[fact] ? held : total[fact];
            } else {
                total[fact] += held;
            }
        }
    }

    Dictionary joint;
    joint["joint_passes"] = total[CENSUS_JOINT_PASSES];
    joint["joint_members"] = total[CENSUS_JOINT_MEMBERS];
    joint["cells_relayed"] = total[CENSUS_CELLS_RELAYED];
    joint["cells_substituted"] = total[CENSUS_CELLS_SUBSTITUTED];
    joint["heal_snaps"] = total[CENSUS_HEAL_SNAPS];
    joint["linger_held"] = total[CENSUS_LINGER_HELD];

    Dictionary out;
    out["entities"] = int64_t(stepped.size());
    out["corrections"] = total[CENSUS_CORRECTIONS];
    out["max_replay_depth"] = total[CENSUS_MAX_REPLAY_DEPTH];
    out["consumed"] = total[CENSUS_CONSUMED];
    out["missing"] = total[CENSUS_MISSING];
    out["folded"] = total[CENSUS_FOLDED];
    out["joint"] = joint;
    out["timelines"] = lagcomp_core.timeline_registered();
    return out;
}

void NetwMultiplayer::advance_frame() {
    frame_counter += 1;
    attribution_feed_push();
}

void NetwMultiplayer::session_defer(
    const Callable &p_fn,
    const StringName &p_key
) {
    settle_queue.schedule(p_fn, p_key);
}

void NetwMultiplayer::session_defer_after(
    const Callable &p_fn,
    const StringName &p_key,
    int p_pumps
) {
    settle_queue.schedule_after(p_fn, p_key, p_pumps);
}

void NetwMultiplayer::settle_advance() {
    settle_queue.advance_windows();
}

void NetwMultiplayer::session_cancel_deferred(const StringName &p_key) {
    settle_queue.cancel(p_key);
}

PackedStringArray NetwMultiplayer::settle_drain() {
    plane.drain_staged();
    return settle_queue.drain();
}

void NetwMultiplayer::settle_clear() {
    settle_queue.clear();
}

int NetwMultiplayer::settle_pending() const {
    return settle_queue.size();
}

bool NetwMultiplayer::settle_has_key(const StringName &p_key) const {
    return settle_queue.has(p_key);
}

int NetwMultiplayer::settle_max_passes() {
    return SettleQueue::MAX_PASSES;
}

void NetwMultiplayer::session_flush_deferred() {
    const PackedStringArray pending = settle_drain();
    if (pending.is_empty()) {
        return;
    }
    NETW_ERROR(
        sys::SESSION,
        "settle did not reach a fixed point in %d passes, still pending: %s",
        settle_max_passes(),
        String(", ").join(pending).utf8().get_data()
    );
}

int64_t NetwMultiplayer::native_prediction_slot(
    const Ref<NetwEntity> &p_entity
) const {
    return prediction_engine.slot_of(p_entity);
}

NetwMultiplayer *NetwMultiplayer::tree_published_session(Node *p_node) {
    Node *walked = p_node;
    while (walked != nullptr) {
        MultiplayerTree *tree = Object::cast_to<MultiplayerTree>(walked);
        if (tree != nullptr) {
            return tree->get_api().ptr();
        }
        walked = walked->get_parent();
    }
    return nullptr;
}

Ref<NetwMultiplayer> NetwMultiplayer::resolve_required(Node *p_node) {
    NetwMultiplayer *session = of(p_node);
    if (session == nullptr) {
        session = tree_published_session(p_node);
    }
    if (session == nullptr) {
        return Ref<NetwMultiplayer>();
    }
    if (session->lagcomp_is_configured()) {
        return Ref<NetwMultiplayer>(session);
    }
    NETW_ERROR(
        sys::PREDICTION,
        "%s needs a LagCompensation node mounted under the MultiplayerTree, "
        "but none was found. Add one as a child of the tree to enable "
        "prediction and rewind.",
        p_node != nullptr ? String(p_node->get_class()) : String("A node")
    );
    return Ref<NetwMultiplayer>();
}

void NetwMultiplayer::predict_reconcile_declaration(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    const Ref<NetwPredictionHandle> handle = p_entity->get_prediction();
    if (handle.is_null()
        || handle->get_archetype() == NetwPredict::ARCHETYPE_NONE) {
        return;
    }
    predict_declare(p_entity->get_rid_handle());
}

void NetwMultiplayer::register_prediction(const Ref<NetwEntity> &p_entity) {
    NETW_ZONE_NC("predict register", colors::PREDICTION);
    if (p_entity.is_null()) {
        return;
    }
    const RID entity = p_entity->get_rid_handle();
    if (predict_engine_seated(entity)) {
        return;
    }
    NetwPredictSlotEngine *engine = memnew(NetwPredictSlotEngine);
    predict_engine_install(entity, engine);
    prediction_engine.slot_register(p_entity);
    const Ref<NetwPredictionHandle> handle = p_entity->get_prediction();
    if (handle.is_valid()) {
        handle->bind_engine(engine);
    }
    engine->attach(session_api(), p_entity);
}

void NetwMultiplayer::unregister_prediction(const Ref<NetwEntity> &p_entity) {
    NETW_ZONE_NC("predict unregister", colors::PREDICTION);
    if (p_entity.is_null()) {
        return;
    }
    const RID entity = p_entity->get_rid_handle();
    NetwPredictSlotEngine *engine = predict_engine_release(entity);
    if (engine == nullptr) {
        return;
    }
    prediction_engine.slot_unregister(p_entity);
    relay_release(entity.get_id());
    engine->release();
    const Ref<NetwPredictionHandle> handle = p_entity->get_prediction();
    if (handle.is_valid()) {
        handle->bind_engine(nullptr);
    }
    godot::memdelete(engine);
}

Error NetwMultiplayer::predict_declare(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "declaring prediction found no entity for %d",
            int(p_entity.get_id())
        );
        return ERR_DOES_NOT_EXIST;
    }
    if (!config_is_consumed(session_decl::KIND_LAGCOMP_CONFIG)) {
        const session_decl::Resolved offered = declaration_book().resolve(
            this,
            session_decl::KIND_LAGCOMP_CONFIG
        );
        if (offered.state == session_decl::READY) {
            predict_awaiting_config.push_back(p_entity);
            declarations_changed();
            return OK;
        }
        if (offered.state != session_decl::ABSENT) {
            declaration_book().report_unresolved(
                this,
                session_decl::KIND_LAGCOMP_CONFIG,
                offered.state,
                "declaring prediction"
            );
            return ERR_UNAVAILABLE;
        }
        config_consume_lagcomp_defaults();
    }
    lagcomp_arm();
    register_prediction(wrapper);
    return OK;
}

void NetwMultiplayer::predict_undeclare(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_valid()) {
        unregister_prediction(wrapper);
    }
}

Ref<NetwPredictionHandle> NetwMultiplayer::prediction_handle(
    const RID &p_entity
) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid()
        ? Ref<NetwPredictionHandle>(wrapper->get_prediction())
        : Ref<NetwPredictionHandle>();
}

void NetwMultiplayer::predict_set_param(
    const RID &p_entity,
    PredictParam p_param,
    const Variant &p_value
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_null()) {
        return;
    }
    switch (p_param) {
        case PREDICT_PARAM_ARCHETYPE:
            return handle->set_archetype(
                static_cast<NetwPredict::Archetype>(int(p_value))
            );
        case PREDICT_PARAM_SCHEDULE:
            return handle->set_schedule(
                static_cast<NetwPredict::Schedule>(int(p_value))
            );
        case PREDICT_PARAM_MISSING_POLICY:
            return handle->set_missing_policy(int(p_value));
        case PREDICT_PARAM_RECOVERY_POLICY:
            return handle->set_recovery_policy(int(p_value));
        case PREDICT_PARAM_SNAP_RESTORE:
            return handle->set_snap_restore(int(p_value));
        case PREDICT_PARAM_CORRECTION_MODE:
            return handle->set_correction_mode(int(p_value));
        case PREDICT_PARAM_TELEPORT_THRESHOLD:
            return handle->set_teleport_threshold(double(p_value));
        case PREDICT_PARAM_DIVERGENCE_EPSILON:
            return handle->set_divergence_epsilon(double(p_value));
        case PREDICT_PARAM_BREACH_RESPONSE:
            return handle->set_breach_response(int(p_value));
        case PREDICT_PARAM_MAX_RESTORE_TICKS:
            return handle->set_max_restore_ticks(int(p_value));
        case PREDICT_PARAM_COLLISION_COOLDOWN_TICKS:
            return handle->set_collision_cooldown_ticks(int(p_value));
        case PREDICT_PARAM_MAX_CONSUME_PER_TICK:
            return handle->set_max_consume_per_tick(int(p_value));
        case PREDICT_PARAM_MAX_CONSUME_LAG_TICKS:
            return handle->set_max_consume_lag_ticks(int(p_value));
        case PREDICT_PARAM_CONSUME_BUFFER_TICKS:
            return handle->set_consume_buffer_ticks(int(p_value));
        case PREDICT_PARAM_REPLAY_BUFFER_DEPTH:
            return handle->set_replay_buffer_depth(int(p_value));
        default:
            NETW_ERR(
                sys::PREDICTION,
                "predict parameter {} names no prediction setting",
                p_param
            );
    }
}

Variant NetwMultiplayer::predict_get_param(
    const RID &p_entity,
    PredictParam p_param
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_null()) {
        return Variant();
    }
    switch (p_param) {
        case PREDICT_PARAM_ARCHETYPE:
            return handle->get_archetype();
        case PREDICT_PARAM_SCHEDULE:
            return handle->get_schedule();
        case PREDICT_PARAM_MISSING_POLICY:
            return handle->get_missing_policy();
        case PREDICT_PARAM_RECOVERY_POLICY:
            return handle->get_recovery_policy();
        case PREDICT_PARAM_SNAP_RESTORE:
            return handle->get_snap_restore();
        case PREDICT_PARAM_CORRECTION_MODE:
            return handle->get_correction_mode();
        case PREDICT_PARAM_TELEPORT_THRESHOLD:
            return handle->get_teleport_threshold();
        case PREDICT_PARAM_DIVERGENCE_EPSILON:
            return handle->get_divergence_epsilon();
        case PREDICT_PARAM_BREACH_RESPONSE:
            return handle->get_breach_response();
        case PREDICT_PARAM_MAX_RESTORE_TICKS:
            return handle->get_max_restore_ticks();
        case PREDICT_PARAM_COLLISION_COOLDOWN_TICKS:
            return handle->get_collision_cooldown_ticks();
        case PREDICT_PARAM_MAX_CONSUME_PER_TICK:
            return handle->get_max_consume_per_tick();
        case PREDICT_PARAM_MAX_CONSUME_LAG_TICKS:
            return handle->get_max_consume_lag_ticks();
        case PREDICT_PARAM_CONSUME_BUFFER_TICKS:
            return handle->get_consume_buffer_ticks();
        case PREDICT_PARAM_REPLAY_BUFFER_DEPTH:
            return handle->get_replay_buffer_depth();
        default:
            return Variant();
    }
}

void NetwMultiplayer::predict_set_sensor_callback(
    const RID &p_entity,
    const StringName &p_name,
    const Callable &p_callback
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_null()) {
        return;
    }
    Dictionary sensors = handle->get_sensors();
    if (p_callback.is_valid()) {
        sensors[p_name] = p_callback;
    } else {
        sensors.erase(p_name);
    }
    handle->set_sensors(sensors);
}

void NetwMultiplayer::predict_set_witness_callback(
    const RID &p_entity,
    const Callable &p_callback
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_valid()) {
        handle->set_witness_contacts(p_callback);
    }
}

void NetwMultiplayer::predict_set_corridor_callback(
    const RID &p_entity,
    const Callable &p_callback
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_valid()) {
        handle->set_transport_corridor(p_callback);
    }
}

void NetwMultiplayer::predict_set_simulate_callback(
    const RID &p_entity,
    const Callable &p_callback
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_valid()) {
        handle->set_simulate(p_callback);
    }
}

Error NetwMultiplayer::predict_bind_owner(
    const RID &p_entity,
    Object *p_owner
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_owner == nullptr) {
        return ERR_INVALID_DATA;
    }
    if (!prediction_engine.slot_bind_owner(wrapper, p_owner)) {
        return ERR_DOES_NOT_EXIST;
    }
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_valid() && !handle->get_simulate().is_valid()
        && p_owner->has_method(StringName("_network_tick"))) {
        handle->set_simulate(Callable(p_owner, StringName("_network_tick")));
    }
    return OK;
}

void NetwMultiplayer::predict_unbind_owner(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_valid()) {
        prediction_engine.slot_unbind_owner(wrapper);
    }
}

Error NetwMultiplayer::predict_island_add(
    const RID &p_entity,
    const RID &p_other
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    const Ref<NetwEntity> member = entity_get_view(p_other);
    if (handle.is_null() || member.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    handle->get_island()->add(member);
    return OK;
}

void NetwMultiplayer::predict_island_remove(
    const RID &p_entity,
    const RID &p_other
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    const Ref<NetwEntity> member = entity_get_view(p_other);
    if (handle.is_valid() && member.is_valid()) {
        handle->get_island()->remove(member);
    }
}

Error NetwMultiplayer::predict_island_set_param(
    const RID &p_entity,
    IslandParam p_param,
    const Variant &p_value
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (handle.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    const Ref<NetwPredictIsland> island = handle->get_island();
    switch (p_param) {
        case ISLAND_PARAM_APPROXIMATE:
            island->set_approximate(bool(p_value));
            return OK;
        case ISLAND_PARAM_EXACT_CLAIM:
            island->set_exact_claim(bool(p_value));
            return OK;
        case ISLAND_PARAM_RECONCILE:
            island->set_reconcile(int(p_value));
            return OK;
        case ISLAND_PARAM_PROMOTION:
            island->set_promotion(int(p_value));
            return OK;
        case ISLAND_PARAM_PROMOTION_COUNT:
            island->set_promotion_count(MAX(0, int(p_value)));
            return OK;
        case ISLAND_PARAM_PROMOTION_METERS:
            island->set_promotion_meters(MAX(0.0, double(p_value)));
            return OK;
        case ISLAND_PARAM_PACING:
            island->set_pacing(int(p_value));
            return OK;
        case ISLAND_PARAM_INPUT_DELAY:
            island->set_input_delay_ticks(MAX(0, int(p_value)));
            return OK;
        default:
            return ERR_INVALID_PARAMETER;
    }
}

Error NetwMultiplayer::predict_island_set_member_param(
    const RID &p_entity,
    const RID &p_member,
    MemberParam p_param,
    const Variant &p_value
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    const Ref<NetwEntity> other = entity_get_view(p_member);
    if (handle.is_null() || other.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    const Ref<NetwPredictIsland> island = handle->get_island();
    if (!island->has_member(other)) {
        return ERR_INVALID_PARAMETER;
    }
    switch (p_param) {
        case MEMBER_PARAM_FIDELITY:
            island->set_fidelity(
                other,
                static_cast<NetwPredict::Fidelity>(int(p_value))
            );
            return OK;
        case MEMBER_PARAM_PREDICTOR:
            island->predict_commands(other, p_value);
            return OK;
        default:
            return ERR_INVALID_PARAMETER;
    }
}

Variant NetwMultiplayer::predict_island_get_member_param(
    const RID &p_entity,
    const RID &p_member,
    MemberParam p_param
) {
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    const Ref<NetwEntity> other = entity_get_view(p_member);
    if (handle.is_null() || other.is_null()) {
        return Variant();
    }
    const Ref<NetwPredictIsland> island = handle->get_island();
    if (!island->has_member(other)) {
        return Variant();
    }
    switch (p_param) {
        case MEMBER_PARAM_FIDELITY: {
            const int declared = island->fidelity_of(other);
            return declared >= 0 ? declared : int(NetwPredict::FIDELITY_PROXY);
        }
        case MEMBER_PARAM_PREDICTOR:
            return island->predictor_of(other);
        default:
            return Variant();
    }
}

Variant NetwMultiplayer::predict_sensor_sample(
    const RID &p_entity,
    const StringName &p_name,
    const Variant &p_default
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return p_default;
    }
    const int64_t slot = prediction_engine.slot_of(wrapper);
    if (slot < 0) {
        return p_default;
    }
    return prediction_engine.sensor_samples(slot).get(p_name, p_default);
}

void NetwMultiplayer::predict_notify_contact(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    const Ref<NetwPredictionHandle> handle = prediction_handle(p_entity);
    if (wrapper.is_null() || handle.is_null()) {
        return;
    }
    const int64_t slot = prediction_engine.slot_of(wrapper);
    if (slot < 0) {
        return;
    }
    prediction_engine.notify_contact(
        slot,
        handle->get_island(),
        handle->get_witness_contacts().is_valid(),
        handle->get_collision_cooldown_ticks()
    );
}

Error NetwMultiplayer::lagcomp_timeline_declare(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    lagcomp_core.timeline_register(wrapper, NetwTimeline::DEFAULT_LIMIT);
    return OK;
}

void NetwMultiplayer::lagcomp_timeline_undeclare(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_valid()) {
        lagcomp_core.timeline_unregister(wrapper);
    }
}

Ref<NetwTimeline> NetwMultiplayer::lagcomp_timeline_of(
    const RID &p_entity
) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return Ref<NetwTimeline>();
    }
    return lagcomp_core.timeline_history(
        lagcomp_core.timeline_slot_of(wrapper)
    );
}

static const char *OBSERVE_KEY_PREFIX = "lagcomp-observe-node?";

void NetwMultiplayer::settle_observe(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    const int64_t instance = int64_t(p_node->get_instance_id());
    session_defer(
        callable_mp(this, &NetwMultiplayer::observe_node_entity_ref)
            .bind(gd::weak_ref(p_node)),
        StringName(String(OBSERVE_KEY_PREFIX) + String::num_int64(instance))
    );
}

} // namespace netw
