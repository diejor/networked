#include "netw/api/sync_pipeline.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_config.hpp"
#include "netw/colors.hpp"
#include "netw/entity/control.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/script/model.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_ENTITY_LIVE = "entity_live";
const char *CONTRACT_KEY_PREFIX = "sync-prediction-contract?";
const char *UNROUTABLE_META = "_netw_unroutable_warned";

const int LANE_ROW = 0;
const int LANE_WINDOW = 1;
const int LANE_RETAINED = 2;

} // namespace

NetwMultiplayer *SyncPipeline::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

Object *SyncPipeline::api() const {
    return gd::instance_from_id(api_id);
}

void SyncPipeline::set_core(Object *p_core) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_core);
    core_id = gd::instance_id(plane);
    if (plane == nullptr) {
        return;
    }
    const Callable live
        = callable_mp(plane, &NetwMultiplayer::sync_pipeline_on_entity_live);
    if (!plane->is_connected(SIG_ENTITY_LIVE, live)) {
        plane->connect(SIG_ENTITY_LIVE, live);
    }
}

void SyncPipeline::set_api(Object *p_api) {
    api_id = gd::instance_id(p_api);
}

void SyncPipeline::set_sync_model(NetwSyncModel *p_model) {
    sync_model = p_model;
}

void SyncPipeline::set_channels(
    int64_t p_row,
    int64_t p_row_window,
    int64_t p_row_delta,
    int64_t p_property,
    int64_t p_signal
) {
    channel_row = p_row;
    channel_row_window = p_row_window;
    channel_row_delta = p_row_delta;
    channel_property = p_property;
    channel_signal = p_signal;
}

void SyncPipeline::set_stage_seams(
    const Callable &p_encode,
    const Callable &p_decode
) {
    encode_stage_seam = p_encode;
    decode_stage_seam = p_decode;
}

void SyncPipeline::set_tap_seam(const Callable &p_tap) {
    tap_seam = p_tap;
}

ReplicationSend *SyncPipeline::row_send() {
    NetwMultiplayer *plane = core();
    if (!row_sender_armed && plane != nullptr) {
        row_sender_armed = true;
        row_sender.set_encode_stage(
            callable_mp(plane, &NetwMultiplayer::sync_pipeline_stage_row_frame)
        );
    }
    return &row_sender;
}

PackedByteArray SyncPipeline::stage_row_frame(
    int64_t p_peer,
    int64_t p_frame_tick,
    int64_t,
    int64_t,
    int64_t,
    const PackedByteArray &p_bytes
) {
    return run_encode_stage(p_peer, p_frame_tick, p_bytes);
}

PackedByteArray SyncPipeline::run_encode_stage(
    int64_t p_peer,
    int64_t p_tick,
    const PackedByteArray &p_bytes
) {
    NETW_ZONE_NC("SyncPipeline encode stage", colors::CODEC);
    NETW_ZONE_VALUE(p_bytes.size());
    PackedByteArray bytes = p_bytes;
    if (encode_stage_seam.is_valid()) {
        bytes = encode_stage_seam.call(p_peer, p_bytes, p_tick);
    }
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->event_wants(EventPlane::SYNC_ENCODE, 0)) {
        Dictionary detail;
        detail[StringName("bytes")] = bytes.size();
        plane->event_emit(
            EventPlane::SYNC_ENCODE,
            0,
            detail,
            StringName(),
            p_peer,
            OK,
            Dictionary()
        );
    }
    return bytes;
}

Error SyncPipeline::decode_stage_body() {
    if (stage_binding.is_null()) {
        return ERR_INVALID_DATA;
    }
    if (stage_lane == LANE_WINDOW) {
        stage_decoded = stage_binding->apply_window_frame(
            row_send(),
            stage_payload,
            stage_arrival
        );
    } else if (stage_lane == LANE_RETAINED) {
        stage_decoded = stage_binding->apply_retained_row(
            row_send(),
            stage_payload,
            stage_arrival
        );
    } else {
        stage_decoded = stage_binding->apply_row_frame(
            row_send(),
            stage_payload,
            stage_arrival
        );
    }
    return stage_decoded.is_empty() ? ERR_INVALID_DATA : OK;
}

Error SyncPipeline::run_decode_stage(
    const RID &p_entity,
    int64_t p_comp,
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("SyncPipeline decode stage", colors::CODEC);
    NETW_ZONE_VALUE(p_payload.size());
    NetwMultiplayer *plane = core();
    Error verdict = OK;
    if (!decode_stage_seam.is_valid() || plane == nullptr) {
        verdict = decode_stage_body();
    } else {
        verdict = Error(
            int(decode_stage_seam.call(
                p_entity,
                p_comp,
                0,
                -1,
                p_payload,
                callable_mp(
                    plane,
                    &NetwMultiplayer::sync_pipeline_decode_stage_body
                )
            ))
        );
    }
    if (plane != nullptr) {
        const int64_t route = plane->get_liveness_core()->route_of(p_entity);
        if (plane->event_wants(EventPlane::SYNC_DECODE, route)) {
            Dictionary detail;
            detail[StringName("comp")] = p_comp;
            plane->event_emit(
                EventPlane::SYNC_DECODE,
                route,
                detail,
                StringName(),
                0,
                verdict,
                Dictionary()
            );
        }
    }
    return verdict;
}

void SyncPipeline::register_derived(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    const Ref<Script> script = p_node->get_script();
    if (script.is_null()) {
        return;
    }
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        if (bindings[at]->node() == p_node) {
            return;
        }
    }
    Object *shell = api();
    const Ref<NetwPropertySet> state = property_set_builder::from_script(
        script,
        NetwPropertySet::RECORD_STATE,
        shell,
        p_node
    );
    if (state.is_valid()) {
        register_property_set(p_node, state);
    }
    const Ref<NetwPropertySet> input = property_set_builder::from_script(
        script,
        NetwPropertySet::RECORD_INPUT,
        shell,
        p_node
    );
    if (input.is_valid()) {
        register_property_set(p_node, input);
    }
    queue_prediction_contract_check(p_node, state, input);
    const Ref<NetwPropertySet> broadcast = property_set_builder::from_script(
        script,
        NetwPropertySet::RECORD_BROADCAST,
        shell,
        p_node
    );
    if (broadcast.is_valid()) {
        register_property_set(p_node, broadcast);
    }
}

void SyncPipeline::note_schema_seal(const Ref<NetwPropertySet> &p_set) {
    const NetwMultiplayer *plane = core();
    if (plane != nullptr
        && plane->session_get_state()
            != NetwMultiplayer::SESSION_STATE_OFFLINE) {
        return;
    }
    schemas_sealed_before_open[p_set->identity_hash()] = true;
}

int64_t SyncPipeline::sealed_schema_identity() const {
    uint64_t fold = 0;
    for (const KeyValue<int64_t, bool> &entry : schemas_sealed_before_open) {
        fold ^= uint64_t(entry.key);
    }
    return int64_t(fold);
}

Error SyncPipeline::register_property_set(
    Node *p_node,
    const Ref<NetwPropertySet> &p_set
) {
    if (p_node == nullptr || p_set.is_null() || !p_set->get_sealed()) {
        return ERR_INVALID_DATA;
    }
    note_schema_seal(p_set);
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        const Ref<NetwPropertySetBinding> held = bindings[at];
        if (held->node() != p_node
            || held->set->get_record() != p_set->get_record()) {
            continue;
        }
        if (held->set == p_set) {
            bind_declaration(held);
            return OK;
        }
        drop_declaration(held);
        const Ref<NetwPropertySetBinding> replacement
            = NetwPropertySetBinding::create(p_set, p_node);
        capture_declaration_key(replacement);
        bindings[at] = replacement;
        bind_declaration(replacement);
        return OK;
    }
    const Ref<NetwPropertySetBinding> created
        = NetwPropertySetBinding::create(p_set, p_node);
    capture_declaration_key(created);
    bindings.push_back(created);
    bind_declaration(created);
    if (p_set->get_record() == NetwPropertySet::RECORD_STATE) {
        register_state_timeline(p_node);
    }
    return OK;
}

void SyncPipeline::queue_prediction_contract_check(
    Node *p_node,
    const Ref<NetwPropertySet> &p_state,
    const Ref<NetwPropertySet> &p_input
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_state.is_null() || p_input.is_null()
        || p_state->get_columns().is_empty()
        || p_input->get_columns().is_empty()) {
        return;
    }
    PackedStringArray parts;
    const TypedArray<NetwPropertySetColumn> state_columns
        = p_state->get_columns();
    for (int at = 0; at < state_columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = state_columns[at];
        parts.push_back(String("s:") + String(column->get_key()));
    }
    const TypedArray<NetwPropertySetColumn> input_columns
        = p_input->get_columns();
    for (int at = 0; at < input_columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = input_columns[at];
        parts.push_back(String("i:") + String(column->get_key()));
    }
    const int64_t config_hash = int64_t(String("\n").join(parts).hash());
    const int64_t instance_id = int64_t(gd::instance_id(p_node));
    const HashMap<int64_t, int64_t>::ConstIterator seen
        = contract_hashes.find(instance_id);
    if (seen && seen->value == config_hash) {
        return;
    }
    contract_hashes[instance_id] = config_hash;
    plane->session_defer(
        callable_mp(
            plane,
            &NetwMultiplayer::sync_pipeline_report_missing_prediction_component
        )
            .bind(p_node, config_hash),
        StringName(vformat("%s%d", String(CONTRACT_KEY_PREFIX), instance_id))
    );
}

void SyncPipeline::report_missing_prediction_component(
    Node *p_node,
    int64_t p_config_hash
) {
    Node *node = p_node;
    if (node == nullptr) {
        return;
    }
    const HashMap<int64_t, int64_t>::ConstIterator seen
        = contract_hashes.find(int64_t(gd::instance_id(node)));
    if (!seen || seen->value != p_config_hash) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid()) {
        const Ref<NetwPredictionHandle> prediction = entity->get_prediction();
        if (prediction.is_valid() && prediction->is_registered()) {
            return;
        }
    }
    if (authoring::declares_prediction(node)) {
        return;
    }
    const String entity_name = entity.is_valid()
        ? String(entity->get_entity_id())
        : String(node->get_name());
    NETW_WARN(
        sys::PREDICTION,
        "%s",
        missing_prediction_component_message(entity_name)
    );
}

String SyncPipeline::missing_prediction_component_message(
    const String &p_entity_name
) {
    return String("Prediction: ") + p_entity_name
        + String(
               " declares state() and input() fields but declares no "
               "prediction. Its controller will author commands without "
               "simulating the predicted state. Fill the prediction block on "
               "its MultiplayerSynchronizer or register prediction from code."
        );
}

void SyncPipeline::register_state_timeline(Node *p_node) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->get_unique_id() != 1) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_valid()) {
        plane->lagcomp_timeline_declare(plane->entity_of(entity->get_owner()));
    }
}

bool SyncPipeline::holds_state_binding(const Ref<NetwEntity> &p_entity) const {
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        const Ref<NetwPropertySetBinding> held = bindings[at];
        if (held->set->get_record() != NetwPropertySet::RECORD_STATE) {
            continue;
        }
        if (NetwEntity::of(held->node()) == p_entity) {
            return true;
        }
    }
    return false;
}

void SyncPipeline::reconcile_state_timeline(Node *p_node) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->get_unique_id() != 1) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null() || holds_state_binding(entity)) {
        return;
    }
    plane->lagcomp_timeline_undeclare(entity->get_rid_handle());
}

void SyncPipeline::reconcile_dropped_state_timelines() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->get_unique_id() != 1) {
        state_binding_dropped.clear();
        return;
    }
    for (uint32_t at = 0; at < state_binding_dropped.size(); ++at) {
        const RID entity = state_binding_dropped[at];
        const Ref<NetwEntity> wrapper
            = Object::cast_to<NetwEntity>(plane->entity_get_view(entity).ptr());
        if (wrapper.is_null() || holds_state_binding(wrapper)) {
            continue;
        }
        plane->lagcomp_timeline_undeclare(entity);
    }
    state_binding_dropped.clear();
}

Ref<NetwPropertySetBinding> SyncPipeline::derived_binding(
    Node *p_node,
    int64_t p_record
) {
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        if (bindings[at]->node() == p_node
            && bindings[at]->set->get_record() == p_record) {
            return bindings[at];
        }
    }
    return Ref<NetwPropertySetBinding>();
}

void SyncPipeline::unregister_derived(Node *p_node) {
    bool dropped_state = false;
    for (uint32_t at = bindings.size(); at > 0; --at) {
        const Ref<NetwPropertySetBinding> held = bindings[at - 1];
        if (held->node() != p_node) {
            continue;
        }
        drop_declaration(held);
        dropped_state = dropped_state
            || held->set->get_record() == NetwPropertySet::RECORD_STATE;
        bindings.remove_at(at - 1);
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (!dropped_state || entity.is_null()) {
        return;
    }
    state_binding_dropped.push_back(entity->get_rid_handle());
}

void SyncPipeline::prune_derived() {
    for (uint32_t at = bindings.size(); at > 0; --at) {
        if (bindings[at - 1]->node() == nullptr) {
            drop_declaration(bindings[at - 1]);
            bindings.remove_at(at - 1);
        }
    }
}

TypedArray<NetwPropertySetBinding> SyncPipeline::derived_group(
    int64_t p_route
) {
    TypedArray<NetwPropertySetBinding> out;
    if (sync_model == nullptr) {
        return out;
    }
    const TypedArray<RefCounted> found
        = sync_model->route_bindings(p_route, NetwSyncModel::KIND_DERIVED);
    for (int at = 0; at < found.size(); ++at) {
        out.push_back(found[at]);
    }
    return out;
}

void SyncPipeline::capture_declaration_key(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    Node *node = p_binding->node();
    if (node == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        return;
    }
    p_binding->order_key
        = StringName(String(entity->get_owner()->get_path_to(node)));
    p_binding->comp = entity->comp_of(node);
}

void SyncPipeline::bind_declaration(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    NetwMultiplayer *plane = core();
    Node *node = p_binding->node();
    if (plane == nullptr || node == nullptr || sync_model == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null()) {
        return;
    }
    if (String(p_binding->order_key).is_empty()) {
        capture_declaration_key(p_binding);
    }
    const RID rid = entity->get_rid_handle();
    int64_t route = plane->get_liveness_core()->route_of(rid);
    if (route <= 0) {
        route = entity->get_route();
    }
    if (route <= 0 || String(p_binding->order_key).is_empty()) {
        return;
    }
    p_binding->route = route;
    sync_model->declare(
        route,
        NetwSyncModel::KIND_DERIVED,
        p_binding->order_key,
        p_binding->comp,
        p_binding->set->get_rid_handle(),
        p_binding->set->get_record(),
        p_binding->set->wire_hash(),
        p_binding->set->get_policy(),
        p_binding->set->get_audience()
    );
    sync_model->attach(
        route,
        NetwSyncModel::KIND_DERIVED,
        p_binding->order_key,
        p_binding->set->get_record(),
        p_binding
    );
}

void SyncPipeline::drop_declaration(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    if (sync_model != nullptr && p_binding->route > 0
        && !String(p_binding->order_key).is_empty()) {
        sync_model->detach(
            p_binding->route,
            NetwSyncModel::KIND_DERIVED,
            p_binding->order_key,
            p_binding->set->get_record()
        );
        sync_model->drop(
            p_binding->route,
            NetwSyncModel::KIND_DERIVED,
            p_binding->order_key,
            p_binding->set->get_record()
        );
    }
    p_binding->route = 0;
}

void SyncPipeline::recapture_entity(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        Node *node = bindings[at]->node();
        if (node == nullptr) {
            continue;
        }
        const Ref<NetwEntity> found = NetwEntity::of(node);
        if (found.is_valid()
            && found->get_rid_handle() == p_entity->get_rid_handle()) {
            capture_declaration_key(bindings[at]);
        }
    }
}

void SyncPipeline::on_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        Node *node = bindings[at]->node();
        if (node == nullptr) {
            continue;
        }
        const Ref<NetwEntity> found = NetwEntity::of(node);
        if (found.is_valid() && p_entity.is_valid()
            && found->get_rid_handle() == p_entity->get_rid_handle()) {
            bindings[at]->route = p_route;
            bind_declaration(bindings[at]);
        }
    }
    settle_entity_column(p_route, true);
}

void SyncPipeline::on_route_retired(int64_t p_route) {
    settle_entity_column(p_route, false);
}

void SyncPipeline::settle_entity_column(int64_t p_route, bool p_live) {
    if (!row_sender_armed) {
        return;
    }
    if (p_live) {
        row_sender.entity_bind(p_route);
    } else {
        row_sender.entity_tombstone(p_route);
    }
    const LocalVector<repl::EntityWrite> completed
        = row_sender.entity_release(p_route);
    for (uint32_t at = 0; at < completed.size(); ++at) {
        const repl::EntityWrite &write = completed[at];
        for (uint32_t which = 0; which < bindings.size(); ++which) {
            const Ref<NetwPropertySetBinding> &binding = bindings[which];
            if (binding->get_route() != write.slot.route) {
                continue;
            }
            binding->write_entity_column(
                int64_t(write.slot.column),
                write.route
            );
        }
    }
}

void SyncPipeline::pump(int64_t p_tick) {
    NETW_ZONE_NC("SyncPipeline pump", colors::WIRE);
    NETW_ZONE_VALUE(p_tick);
    NetwMultiplayer *plane = core();
    if (plane == nullptr || sync_model == nullptr) {
        return;
    }
    prune_derived();
    Array live;
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        live.push_back(bindings[at]);
    }
    flush_row_offers(plane->sync_pump_offers(
        sync_model,
        row_send(),
        live,
        p_tick,
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_bind_declaration),
        tap_seam
    ));
}

void SyncPipeline::flush_row_offers(
    const LocalVector<repl::RowOffer> &p_offers
) {
    NETW_ZONE_NC("SyncPipeline flush row offers", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    plane->sync_flush_offers(
        row_send(),
        p_offers,
        channel_row,
        channel_row_window,
        channel_row_delta
    );
}

void SyncPipeline::commit_pending_masked(int64_t p_peer_id, int64_t p_seq) {
    if (row_sender_armed) {
        row_sender.commit(p_peer_id, p_seq);
    }
}

void SyncPipeline::note_peer_ack(
    int64_t p_peer_id,
    int64_t p_acked_seq,
    uint32_t p_history
) {
    if (!row_sender_armed) {
        return;
    }
    NetwMultiplayer *plane = core();
    const ClockEngine *clock
        = plane != nullptr ? &plane->clock_engine() : nullptr;
    row_sender.acknowledge(
        p_peer_id,
        p_acked_seq,
        p_history,
        clock != nullptr ? clock->rtt_avg() * 1000.0 : 0.0,
        clock != nullptr ? clock->rtt_jitter() * 1000.0 : 0.0,
        clock != nullptr ? int64_t(clock->get_tick()) : 0
    );
}

Dictionary SyncPipeline::resolve_send(Node *p_node) {
    Dictionary out;
    NetwMultiplayer *plane = core();
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        sends_dropped_unroutable += 1;
        return out;
    }
    if (plane == nullptr) {
        return out;
    }
    const int64_t route = plane->liveness_route_of(entity.ptr());
    if (route <= 0) {
        sends_dropped_not_live += 1;
        return out;
    }
    const int64_t comp = entity->comp_of(p_node);
    out[StringName("entity")] = entity;
    out[StringName("route")] = route;
    out[StringName("comp")] = comp;
    out[StringName("path")] = comp == 255
        ? String(entity->relative_path(entity->get_owner(), p_node))
        : String();
    return out;
}

void SyncPipeline::send_entity_event(
    const Dictionary &p_frame,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = p_frame[StringName("entity")];
    const PackedInt32Array recipients = NetwSyncModel::event_recipients(
        plane->has_server_role(),
        plane->get_unique_id(),
        0,
        plane->rpc_get_recipients(entity)
    );
    for (int at = 0; at < recipients.size(); ++at) {
        plane->send_to(
            recipients[at],
            int64_t(p_frame[StringName("route")]),
            p_channel,
            p_payload,
            p_reliable,
            int64_t(p_frame[StringName("comp")]),
            String(p_frame[StringName("path")]),
            false
        );
    }
}

Array SyncPipeline::gather_stage() {
    Array values;
    if (stage_node != nullptr) {
        values.push_back(stage_node->get(stage_property));
    }
    return values;
}

void SyncPipeline::send_property(Node *p_node, const StringName &p_property) {
    NETW_ZONE_NC("SyncPipeline send property", colors::WIRE);
    NetwMultiplayer *plane = core();
    if (p_node == nullptr || plane == nullptr) {
        return;
    }
    const Dictionary frame = resolve_send(p_node);
    if (frame.is_empty()) {
        if (NetwEntity::of(p_node).is_null()
            && !p_node->has_meta(StringName(UNROUTABLE_META))) {
            p_node->set_meta(StringName(UNROUTABLE_META), true);
            NETW_WARN(
                sys::WIRE,
                "sync_property: Node '%s' is not part of any NetwEntity.",
                p_node->get_name()
            );
        }
        return;
    }
    if (!gd::has_property(p_node, p_property)) {
        NETW_WARN(
            sys::WIRE,
            "sync_property: Property '%s' does not exist on '%s'.",
            p_property,
            p_node->get_name()
        );
        return;
    }
    const Ref<Script> script = p_node->get_script();
    const Ref<NetwPropertyConfig> opt = script.is_valid()
        ? netw::script::model::get_property_config(script, p_property)
        : Ref<NetwPropertyConfig>();
    if (opt.is_null()) {
        const int64_t local_id = plane->get_unique_id();
        if (local_id != 1 && local_id != p_node->get_multiplayer_authority()) {
            NETW_WARN(
                sys::WIRE,
                "sync_property: Non-authority peer cannot sync unregistered "
                "property '%s'.",
                p_property
            );
            return;
        }
    }

    const Ref<NetwPropertySet> set
        = property_set_builder::from_property_config(p_property, opt);
    if (set.is_null() || set->get_columns().is_empty()) {
        return;
    }

    wire::WriteStream stream;
    const Ref<NetwEntity> entity = frame[StringName("entity")];
    if (!netw::script::model::write_token(
            stream,
            encode_prop_val(entity, p_node, p_property)
        )) {
        return;
    }

    stage_node = p_node;
    stage_property = p_property;
    const Array values = plane->run_gather_set(
        entity->get_rid_handle(),
        int64_t(frame[StringName("comp")]),
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_gather_stage)
    );
    stage_node = nullptr;
    if (values.size() != 1) {
        return;
    }
    const Ref<NetwPropertySetColumn> column = set->get_columns()[0];
    Array quantizers;
    quantizers.push_back(column->get_quantizer());
    Array types;
    types.push_back(
        netw::script::model::get_node_property_type(p_node, p_property)
    );
    if (!call_args::values_write(stream, values, quantizers, types)
        || !stream.align_verify()) {
        return;
    }

    send_entity_event(
        frame,
        set->get_channel(),
        stream.to_bytes(),
        set->get_reliable()
    );
}

void SyncPipeline::send_signal(
    Node *p_node,
    const StringName &p_signal,
    const Array &p_args
) {
    NETW_ZONE_NC("SyncPipeline send signal", colors::WIRE);
    NetwMultiplayer *plane = core();
    if (p_node == nullptr || plane == nullptr) {
        return;
    }
    const Dictionary frame = resolve_send(p_node);
    if (frame.is_empty()) {
        if (NetwEntity::of(p_node).is_null()
            && !p_node->has_meta(StringName(UNROUTABLE_META))) {
            p_node->set_meta(StringName(UNROUTABLE_META), true);
            NETW_WARN(
                sys::WIRE,
                "send_signal: Node '%s' is not part of any NetwEntity.",
                p_node->get_name()
            );
        }
        return;
    }
    if (!p_node->has_signal(p_signal)) {
        NETW_WARN(
            sys::WIRE,
            "send_signal: Signal '%s' does not exist on '%s'.",
            p_signal,
            p_node->get_name()
        );
        return;
    }

    const Ref<Script> script = p_node->get_script();
    const Ref<NetwMemberConfig> opt = script.is_valid()
        ? netw::script::model::get_signal_config(script, p_signal)
        : Ref<NetwMemberConfig>();
    bool reliable = true;
    bool call_local = true;
    if (opt.is_valid()) {
        reliable
            = opt->get_transfer_mode() == NetwMemberConfig::TRANSFER_RELIABLE;
        call_local = opt->get_is_call_local();
    } else {
        const int64_t local_id = plane->get_unique_id();
        if (local_id != 1 && local_id != p_node->get_multiplayer_authority()) {
            NETW_WARN(
                sys::WIRE,
                "send_signal: Non-authority peer cannot emit unregistered "
                "signal '%s'.",
                p_signal
            );
            return;
        }
    }

    if (call_local) {
        Array call_args;
        call_args.push_back(p_signal);
        call_args.append_array(p_args);
        Callable(p_node, StringName("emit_signal")).callv(call_args);
    }

    wire::WriteStream stream;
    const Ref<NetwEntity> entity = frame[StringName("entity")];
    const Array quantizers = opt.is_valid() ? opt->get_quantizers() : Array();
    const Array types = script.is_valid()
        ? netw::script::model::get_signal_arg_types(script, p_signal)
        : Array();
    if (!netw::script::model::write_token(
            stream,
            encode_signal_val(entity, p_node, p_signal)
        )
        || !netw::call_args::values_write(stream, p_args, quantizers, types)
        || !stream.align_verify()) {
        return;
    }

    send_entity_event(frame, channel_signal, stream.to_bytes(), reliable);
}

Variant SyncPipeline::encode_prop_val(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_property
) {
    Ref<Script> script;
    if (p_node != nullptr) {
        script = p_node->get_script();
    }
    if (script.is_valid() && p_entity.is_valid()) {
        if (!p_entity->comp_table().get_poisoned()) {
            const int64_t pid
                = netw::script::model::get_property_id(script, p_property);
            if (pid > 0) {
                return pid;
            }
        }
    }
    return p_property;
}

Variant SyncPipeline::encode_signal_val(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_signal
) {
    Ref<Script> script;
    if (p_node != nullptr) {
        script = p_node->get_script();
    }
    if (script.is_valid() && p_entity.is_valid()) {
        if (!p_entity->comp_table().get_poisoned()) {
            const int64_t sid
                = netw::script::model::get_signal_id(script, p_signal);
            if (sid > 0) {
                return sid;
            }
        }
    }
    return p_signal;
}

bool SyncPipeline::accept_unreliable(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    int64_t p_seq
) {
    return progress.accept_in_datagram(p_sender, p_route, p_channel, p_seq);
}

void SyncPipeline::open_datagram(int64_t p_base_tick, int64_t p_seq) {
    datagram_tick = p_base_tick;
    datagram_seq = p_seq;
    progress.open_datagram();
}

Dictionary SyncPipeline::gather_payload(
    Node *p_node,
    const Ref<NetwPropertySet> &p_set
) {
    Dictionary out;
    if (p_node == nullptr || p_set.is_null()) {
        return out;
    }
    const TypedArray<NetwPropertySetColumn> columns = p_set->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        const StringName key = column->get_key();
        if (gd::has_property(p_node, key)) {
            out[key] = p_node->get(key);
        }
    }
    return out;
}

void SyncPipeline::apply_payload(
    Node *p_node,
    const Ref<NetwPropertySet> &p_set,
    const Dictionary &p_payload
) {
    if (p_node == nullptr || p_set.is_null()) {
        return;
    }
    const TypedArray<NetwPropertySetColumn> columns = p_set->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        const StringName key = column->get_key();
        if (p_payload.has(key) && gd::has_property(p_node, key)) {
            p_node->set(key, p_payload[key]);
        }
    }
}

void SyncPipeline::apply_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel,
    int p_lane
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || sync_model == nullptr || p_entity.is_null()) {
        return;
    }
    const int64_t route = plane->liveness_route_of(p_entity.ptr());
    const Ref<NetwPropertySetBinding> binding = sync_model->admit_row(
        route,
        p_ordinal,
        p_sender,
        p_entity->get_controller()
    );
    if (binding.is_null()) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::UNBOUND
        );
        return;
    }
    stage_binding = binding;
    stage_payload = p_payload;
    stage_arrival.ordinal = p_ordinal;
    stage_arrival.base_tick = datagram_tick;
    stage_arrival.seq = p_lane == LANE_ROW ? datagram_seq : -1;
    stage_arrival.life = plane->liveness_route_wire_life(route);
    stage_lane = p_lane;
    stage_decoded = Dictionary();
    const int64_t baseline_drops_before = row_send()->baseline_drops();
    const Error verdict = run_decode_stage(
        p_entity->get_rid_handle(),
        binding->comp,
        p_payload
    );
    const Dictionary decoded = stage_decoded;
    stage_binding = Ref<NetwPropertySetBinding>();
    stage_decoded = Dictionary();
    if (verdict != OK || decoded.is_empty()) {
        const bool baseline_moved
            = row_send()->baseline_drops() > baseline_drops_before;
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            baseline_moved ? wire::Refusal::BASELINE_UNKNOWN
                           : wire::Refusal::MALFORMED
        );
        return;
    }
    sync_model->note_row_applied();
    feed_derived_interpolation(binding, decoded);
}

void SyncPipeline::handle_derived_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(p_entity, p_ordinal, p_payload, p_sender, p_channel, LANE_ROW);
}

void SyncPipeline::handle_window_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(p_entity, p_ordinal, p_payload, p_sender, p_channel, LANE_WINDOW);
}

void SyncPipeline::handle_retained_row(
    const Ref<NetwEntity> &p_entity,
    int64_t p_ordinal,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    apply_row(
        p_entity,
        p_ordinal,
        p_payload,
        p_sender,
        p_channel,
        LANE_RETAINED
    );
}

void SyncPipeline::feed_derived_interpolation(
    const Ref<NetwPropertySetBinding> &p_binding,
    const Dictionary &p_header
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    if (p_binding->set->get_audience() != NetwPropertySet::AUDIENCE_PUBLIC) {
        return;
    }
    Node *node = p_binding->node();
    if (node == nullptr) {
        return;
    }
    int64_t tick = p_header.has(StringName("tick"))
        ? int64_t(p_header[StringName("tick")])
        : -1;
    const bool authoring
        = p_binding->set->get_stamp() != NetwPropertySet::STAMP_NONE
        && tick >= 0;
    if (!authoring) {
        const ClockEngine &clock = plane->clock_engine();
        tick = clock.get_configured() ? clock.get_tick() : 0;
    }
    const Dictionary payload = p_header.has(StringName("payload"))
        ? Dictionary(p_header[StringName("payload")])
        : Dictionary();
    if (payload.is_empty()) {
        const TypedArray<NetwPropertySetColumn> columns
            = p_binding->set->get_columns();
        for (int at = 0; at < columns.size(); ++at) {
            const Ref<NetwPropertySetColumn> column = columns[at];
            if (column->get_lane() != NetwPropertySet::RETAINED) {
                continue;
            }
            const StringName key = column->get_key();
            const Ref<NetwInterpolate> spec
                = netw::script::model::get_node_property_interpolator(
                    node,
                    key
                );
            if (spec.is_valid()) {
                plane->display_record(
                    node,
                    key,
                    node->get(key),
                    tick,
                    spec,
                    false
                );
            }
        }
        return;
    }
    const Array keys = payload.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        const Ref<NetwInterpolate> spec
            = netw::script::model::get_node_property_interpolator(node, key);
        if (spec.is_valid()) {
            plane->display_record(
                node,
                key,
                payload[key],
                tick,
                spec,
                authoring
            );
        }
    }
}

PackedByteArray SyncPipeline::encode_derived_descriptors(int64_t p_route) {
    LocalVector<spawn::DescriptorRow> rows;
    const LocalVector<repl::SetRow> *found
        = sync_model != nullptr ? sync_model->route_rows(p_route) : nullptr;
    if (found != nullptr) {
        for (const repl::SetRow &row : *found) {
            if (row.kind == NetwSyncModel::KIND_DERIVED) {
                spawn::DescriptorRow entry;
                entry.ordinal = uint64_t(row.ordinal);
                entry.schema_hash = uint64_t(uint32_t(row.schema_hash));
                rows.push_back(entry);
            }
        }
    }
    return spawn::Record::write_descriptors(rows);
}

void SyncPipeline::note_derived_schema(
    int64_t p_route,
    const Dictionary &p_descriptors
) {
    if (sync_model != nullptr) {
        sync_model->note_descriptors(p_route, p_descriptors);
    }
}

void SyncPipeline::clear_session() {
    bindings.clear();
    progress.clear();
    row_sender = ReplicationSend();
    row_sender_armed = false;
}

void SyncPipeline::clear_route(int64_t p_route) {
    progress.clear_route(p_route);
    if (row_sender_armed) {
        row_sender.close_route(p_route);
    }
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        if (bindings[at]->route == p_route) {
            bindings[at]->route = 0;
        }
    }
}

void SyncPipeline::clear_peer(int64_t p_peer_id) {
    progress.clear_peer(p_peer_id);
    for (uint32_t at = 0; at < bindings.size(); ++at) {
        bindings[at]->clear_peer(p_peer_id);
    }
    if (row_sender_armed) {
        row_sender.forget_peer(p_peer_id);
    }
}

void SyncPipeline::dispose() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Callable live
        = callable_mp(plane, &NetwMultiplayer::sync_pipeline_on_entity_live);
    if (plane->is_connected(SIG_ENTITY_LIVE, live)) {
        plane->disconnect(SIG_ENTITY_LIVE, live);
    }
}

Dictionary SyncPipeline::counters() const {
    NetwMultiplayer *plane = core();
    const Dictionary model_stats
        = sync_model != nullptr ? sync_model->stats() : Dictionary();
    const Dictionary flush_stats
        = plane != nullptr ? plane->sync_flush_stats() : Dictionary();
    Dictionary out;
    out[StringName("sends_dropped_unroutable")] = sends_dropped_unroutable;
    out[StringName("sends_dropped_not_live")] = sends_dropped_not_live;
    out[StringName("sync_drops_stale")] = progress.stale_count();
    out[StringName("row_frames_dropped_baseline")]
        = row_sender_armed ? row_sender.baseline_drops() : int64_t(0);
    out[StringName("derived_sets_active")] = int64_t(bindings.size());
    out[StringName("derived_frames_in")]
        = int64_t(model_stats.get(StringName("rows_in"), 0));
    out[StringName("drops_derived_no_set")]
        = int64_t(model_stats.get(StringName("drops_no_set"), 0));
    out[StringName("drops_derived_bad_sender")]
        = int64_t(model_stats.get(StringName("drops_bad_sender"), 0));
    out[StringName("drops_derived_schema")]
        = int64_t(model_stats.get(StringName("drops_schema"), 0));
    out[StringName("row_frames_out")]
        = int64_t(flush_stats.get(StringName("row_frames_out"), 0));
    out[StringName("row_frames_full")]
        = int64_t(flush_stats.get(StringName("row_frames_full"), 0));
    out[StringName("row_frames_stage_refused")]
        = int64_t(flush_stats.get(StringName("row_frames_stage_refused"), 0));
    out[StringName("row_frames_ungathered")]
        = int64_t(flush_stats.get(StringName("row_frames_ungathered"), 0));
    out[StringName("retained_frames_out")]
        = int64_t(flush_stats.get(StringName("retained_frames_out"), 0));
    out[StringName("window_frames_out")]
        = int64_t(flush_stats.get(StringName("window_frames_out"), 0));
    out[StringName("window_samples_out")]
        = int64_t(flush_stats.get(StringName("window_samples_out"), 0));
    out[StringName("sync_pump_skips_invalid_node")]
        = int64_t(flush_stats.get(StringName("pump_skips_invalid_node"), 0));
    out[StringName("sync_pump_skips_no_entity")]
        = int64_t(flush_stats.get(StringName("pump_skips_no_entity"), 0));
    out[StringName("sync_pump_skips_no_route")]
        = int64_t(flush_stats.get(StringName("pump_skips_no_route"), 0));
    out[StringName("sync_pump_skips_not_author")]
        = int64_t(model_stats.get(StringName("skips_not_author"), 0));
    out[StringName("sync_pump_skips_no_recipients")]
        = int64_t(model_stats.get(StringName("skips_no_recipients"), 0));
    return out;
}

void SyncPipeline::handle_property_sync(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_comp
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_comp_node == nullptr || p_entity.is_null()) {
        return;
    }
    wire::ReadStream reader(p_payload);
    const Ref<Script> script = p_comp_node->get_script();
    Variant token;
    if (!netw::script::model::read_token(reader, token)) {
        return;
    }
    StringName prop;
    if (token.get_type() == Variant::INT) {
        prop = script.is_valid() ? netw::script::model::get_property_name_by_id(
                                       script,
                                       int64_t(token)
                                   )
                                 : StringName();
    } else {
        prop = StringName(token);
    }
    if (String(prop).is_empty()) {
        return;
    }

    const Ref<NetwPropertyConfig> opt = script.is_valid()
        ? netw::script::model::get_property_config(script, prop)
        : Ref<NetwPropertyConfig>();
    const Ref<NetwPropertySet> set
        = property_set_builder::from_property_config(prop, opt);
    if (set.is_null() || set->get_columns().is_empty()) {
        return;
    }
    const Ref<NetwPropertySetColumn> column = set->get_columns()[0];
    Array quantizers;
    quantizers.push_back(column->get_quantizer());
    Array types;
    types.push_back(
        netw::script::model::get_node_property_type(p_comp_node, prop)
    );
    Array decoded;
    if (!netw::call_args::values_read(reader, quantizers, types, decoded)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }
    if (decoded.is_empty()) {
        return;
    }
    const Variant value = decoded[0];

    if (p_sender != 1 && opt.is_null()) {
        return;
    }
    if (!entity::Control::script_admits(
            p_comp_node,
            prop,
            false,
            p_sender,
            p_entity->get_controller()
        )) {
        NETW_WARN(
            sys::WIRE,
            "unauthorized property sync for '%s' from peer %d",
            prop,
            p_sender
        );
        return;
    }

    int64_t comp = p_comp;
    if (comp < 0) {
        comp = p_entity->comp_of(p_comp_node);
    }
    stage_node = p_comp_node;
    stage_property = prop;
    stage_values = decoded;
    Array staged;
    staged.push_back(value);
    const Error verdict = plane->run_apply_set(
        p_entity->get_rid_handle(),
        comp,
        staged,
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_apply_one_value)
    );
    stage_node = nullptr;
    if (verdict != OK) {
        return;
    }

    const Array interpolators
        = opt.is_valid() ? opt->get_interpolators() : Array();
    if (!interpolators.is_empty()) {
        const Ref<NetwInterpolate> spec = interpolators[0];
        if (spec.is_valid()) {
            plane->display_record(
                p_comp_node,
                prop,
                value,
                receive_tick(),
                spec,
                false
            );
        }
    }

    if (plane->has_server_role()) {
        const int64_t route = plane->liveness_route_of(p_entity.ptr());
        const PackedInt32Array recipients = NetwSyncModel::event_recipients(
            true,
            plane->get_unique_id(),
            p_sender,
            plane->rpc_get_recipients(p_entity)
        );
        for (int at = 0; at < recipients.size(); ++at) {
            plane->send_to(
                recipients[at],
                route,
                channel_property,
                p_payload,
                set->get_reliable(),
                0,
                String(),
                false
            );
        }
    }
}

void SyncPipeline::handle_signal(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_comp_node == nullptr || p_entity.is_null()) {
        return;
    }
    wire::ReadStream reader(p_payload);
    const Ref<Script> script = p_comp_node->get_script();
    Variant token;
    if (!netw::script::model::read_token(reader, token)) {
        return;
    }
    StringName signal_name;
    if (token.get_type() == Variant::INT) {
        signal_name = script.is_valid()
            ? netw::script::model::get_signal_name_by_id(script, int64_t(token))
            : StringName();
    } else {
        signal_name = StringName(token);
    }
    if (String(signal_name).is_empty()) {
        return;
    }

    const Ref<NetwMemberConfig> opt = script.is_valid()
        ? netw::script::model::get_signal_config(script, signal_name)
        : Ref<NetwMemberConfig>();
    bool reliable = true;
    if (opt.is_valid()) {
        reliable
            = opt->get_transfer_mode() == NetwMemberConfig::TRANSFER_RELIABLE;
    }
    const Array quantizers = opt.is_valid() ? opt->get_quantizers() : Array();
    const Array types = script.is_valid()
        ? netw::script::model::get_signal_arg_types(script, signal_name)
        : Array();
    Array args;
    if (!netw::call_args::values_read(reader, quantizers, types, args)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }

    if (p_sender != 1 && opt.is_null()) {
        return;
    }
    if (!entity::Control::script_admits(
            p_comp_node,
            signal_name,
            true,
            p_sender,
            p_entity->get_controller()
        )) {
        NETW_WARN(
            sys::WIRE,
            "unauthorized signal emission '%s' from peer %d",
            signal_name,
            p_sender
        );
        return;
    }

    record_interpolated_signal_args(p_comp_node, args, opt);

    Array call_args;
    call_args.push_back(signal_name);
    call_args.append_array(args);
    Callable(p_comp_node, StringName("emit_signal")).callv(call_args);

    if (plane->has_server_role()) {
        const int64_t route = plane->liveness_route_of(p_entity.ptr());
        const PackedInt32Array recipients = NetwSyncModel::event_recipients(
            true,
            plane->get_unique_id(),
            p_sender,
            plane->rpc_get_recipients(p_entity)
        );
        for (int at = 0; at < recipients.size(); ++at) {
            plane->send_to(
                recipients[at],
                route,
                channel_signal,
                p_payload,
                reliable,
                0,
                String(),
                false
            );
        }
    }
}

void SyncPipeline::record_interpolated_signal_args(
    Node *p_comp_node,
    const Array &p_args,
    const Ref<NetwMemberConfig> &p_opt
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_opt.is_null()
        || p_opt->get_interpolators().is_empty()) {
        return;
    }
    const int64_t tick = receive_tick();
    const Array interpolators = p_opt->get_interpolators();
    for (int at = 0; at < interpolators.size(); ++at) {
        const Ref<NetwInterpolate> spec = interpolators[at];
        if (spec.is_null() || String(spec->get_target()).is_empty()
            || at >= p_args.size()) {
            continue;
        }
        plane->display_record(
            p_comp_node,
            spec->get_target(),
            p_args[at],
            tick,
            spec,
            false
        );
    }
}

Error SyncPipeline::apply_one_value(const Array &p_values) {
    if (p_values.size() != 1 || stage_node == nullptr) {
        return ERR_INVALID_DATA;
    }
    stage_node->set(stage_property, p_values[0]);
    return OK;
}

int64_t SyncPipeline::receive_tick() const {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return 0;
    }
    const ClockEngine &clock = plane->clock_engine();
    if (clock.get_configured()) {
        return clock.get_tick();
    }
    return plane->get_frame_counter();
}

} // namespace netw
