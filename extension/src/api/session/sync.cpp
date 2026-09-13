#include "netw/api/interest_handle.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"

#include "netw/api/participant.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_server.hpp"
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
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/sync_authoring.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_TABLE_RECEIVED = "table_received";

constexpr double SHUTDOWN_NOTIFY_DELAY = 0.5;

} // namespace

ReplicationCore *NetwMultiplayer::get_replication_plane() const {
    return replication_owner;
}

void NetwMultiplayer::replication_flush_all_buffers() {
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->flush_all_buffers();
    }
}

Node *NetwMultiplayer::replication_resolve_comp_node(
    const Ref<NetwEntity> &p_entity,
    int64_t p_comp,
    const String &p_path
) {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->resolve_comp_node(p_entity, p_comp, p_path)
                            : nullptr;
}

Ref<NetwEntity> NetwMultiplayer::replication_adopt_in_place(Node *p_root) {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->adopt_in_place(p_root) : Ref<NetwEntity>();
}

void NetwMultiplayer::replication_handle_table_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->handle_table_frame(p_payload, p_sender);
    }
}

void NetwMultiplayer::replication_settle_reply(
    int64_t p_txn,
    int64_t p_sender,
    int64_t p_route,
    int64_t p_comp,
    const String &p_path
) {
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->settle_reply(p_txn, p_sender, p_route, p_comp, p_path);
    }
}

Error NetwMultiplayer::replication_dispatch(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    const String &p_path,
    int64_t p_sender,
    bool p_reliable,
    int64_t p_seq
) {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->dispatch(
                                  p_route,
                                  p_comp,
                                  p_channel,
                                  p_payload,
                                  p_path,
                                  p_sender,
                                  p_reliable,
                                  p_seq
                              )
                            : ERR_UNCONFIGURED;
}

void NetwMultiplayer::replication_clear_route(int64_t p_route) {
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->clear_route(p_route);
    }
}

void NetwMultiplayer::replication_replay_tables(int64_t p_peer_id) {
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->replay_tables(p_peer_id);
    }
}

Error NetwMultiplayer::receive_carrier(
    const PackedByteArray &p_framed,
    int64_t p_sender,
    bool p_reliable,
    int64_t p_seq,
    int64_t p_base_tick
) {
    NETW_ZONE_NC("session receive carrier", colors::WIRE);
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::WIRE,
            "dropped %d carrier bytes from peer %d, no replication plane",
            int(p_framed.size()),
            int(p_sender)
        );
        return ERR_UNCONFIGURED;
    }
    return plane
        ->receive_carrier(p_framed, p_sender, p_reliable, p_seq, p_base_tick);
}

SyncPipeline *NetwMultiplayer::sync_pipeline() const {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->get_sync_pipeline() : nullptr;
}

int64_t NetwMultiplayer::session_schema_identity() const {
    const SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->sealed_schema_identity() : 0;
}

void NetwMultiplayer::sync_pipeline_on_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->on_entity_live(p_route, p_entity);
    }
}

PackedByteArray NetwMultiplayer::sync_pipeline_stage_row_frame(
    int64_t p_peer,
    int64_t p_frame_tick,
    int64_t p_route,
    int64_t p_comp,
    int64_t p_mask,
    const PackedByteArray &p_bytes
) {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->stage_row_frame(
                                     p_peer,
                                     p_frame_tick,
                                     p_route,
                                     p_comp,
                                     p_mask,
                                     p_bytes
                                 )
                               : PackedByteArray();
}

Error NetwMultiplayer::sync_pipeline_decode_stage_body() {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->decode_stage_body()
                               : ERR_UNCONFIGURED;
}

Array NetwMultiplayer::sync_pipeline_gather_stage() {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->gather_stage() : Array();
}

Error NetwMultiplayer::sync_pipeline_apply_one_value(const Array &p_values) {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->apply_one_value(p_values)
                               : ERR_UNCONFIGURED;
}

void NetwMultiplayer::sync_pipeline_bind_declaration(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->bind_declaration(p_binding);
    }
}

void NetwMultiplayer::sync_pipeline_recapture_entity(
    const Ref<NetwEntity> &p_entity
) {
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->recapture_entity(p_entity);
    }
}

void NetwMultiplayer::sync_pipeline_report_missing_prediction_component(
    Node *p_node,
    int64_t p_config_hash
) {
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->report_missing_prediction_component(p_node, p_config_hash);
    }
}

Variant NetwMultiplayer::sync_pipeline_encode_prop_val(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_property
) {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr
        ? pipeline->encode_prop_val(p_entity, p_node, p_property)
        : Variant();
}

PackedByteArray NetwMultiplayer::sync_pipeline_encode_derived_descriptors(
    int64_t p_route
) {
    SyncPipeline *pipeline = sync_pipeline();
    return pipeline != nullptr ? pipeline->encode_derived_descriptors(p_route)
                               : PackedByteArray();
}

void NetwMultiplayer::sync_pipeline_note_derived_schema(
    int64_t p_route,
    const Dictionary &p_descriptors
) {
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->note_derived_schema(p_route, p_descriptors);
    }
}

Error NetwMultiplayer::sync_send_property(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_property
) {
    Node *node = entity_component_node(p_entity, p_comp);
    NETW_ERR_COND_V(
        node == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::WIRE,
        "property %s addressed component %d of an entity that resolves to no "
        "node",
        String(p_property),
        int(p_comp)
    );
    NETW_ERR_COND_V(
        !gd::has_property(node, p_property),
        ERR_INVALID_DATA,
        sys::WIRE,
        "node %s declares no property %s",
        node->get_name(),
        String(p_property)
    );
    SyncPipeline *pipeline = sync_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        ERR_UNCONFIGURED,
        sys::WIRE,
        "property %s was sent with no sync pipeline installed",
        String(p_property)
    );
    pipeline->send_property(node, p_property);
    return OK;
}

Error NetwMultiplayer::sync_send_signal(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_signal,
    const Array &p_args
) {
    Node *node = entity_component_node(p_entity, p_comp);
    NETW_ERR_COND_V(
        node == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::WIRE,
        "signal %s addressed component %d of an entity that resolves to no "
        "node",
        String(p_signal),
        int(p_comp)
    );
    NETW_ERR_COND_V(
        !node->has_signal(p_signal),
        ERR_INVALID_DATA,
        sys::WIRE,
        "node %s declares no signal %s",
        node->get_name(),
        String(p_signal)
    );
    SyncPipeline *pipeline = sync_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        ERR_UNCONFIGURED,
        sys::WIRE,
        "signal %s was sent with no sync pipeline installed",
        String(p_signal)
    );
    pipeline->send_signal(node, p_signal, p_args);
    return OK;
}

RID NetwMultiplayer::entity_replicate(Object *p_node, Object *p_owner) {
    NETW_ZONE_NC("session spawn replicate", colors::LIVENESS);
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "a node was armed for replication with no spawn pipeline installed"
    );
    const Ref<NetwEntity> wrapper = pipeline->replicate(
        Object::cast_to<Node>(p_node),
        Ref<NetwParticipant>(Object::cast_to<NetwParticipant>(p_owner))
    );
    return wrapper.is_valid() ? wrapper->get_rid_handle() : RID();
}

NodePath NetwMultiplayer::property_path(
    Object *p_source,
    const StringName &p_property,
    Object *p_base
) {
    Node *base = Object::cast_to<Node>(p_base);
    if (base == nullptr) {
        return NodePath();
    }
    const NodePath relative = relative_path(base, p_source);
    if (relative.is_empty()) {
        return NodePath();
    }
    return NodePath(String(relative) + ":" + String(p_property));
}

Error NetwMultiplayer::sync_admit_frame_default(
    int64_t,
    int64_t p_route,
    int64_t,
    int64_t p_channel,
    int64_t,
    int64_t,
    const PackedByteArray &p_payload
) const {
    const Error verdict = entity_frame_verdict(p_route);
    if (verdict != OK) {
        return verdict;
    }
    const bool carries_sync = p_channel == wire::builtin_channel("SYNC")
        || p_channel == wire::builtin_channel("SYNC_ROW")
        || p_channel == wire::builtin_channel("SYNC_ROW_DELTA")
        || p_channel == wire::builtin_channel("SYNC_ROW_WINDOW")
        || p_channel == wire::builtin_channel("SYNC_DELTA");
    if (!carries_sync || p_payload.is_empty()) {
        return ERR_INVALID_DATA;
    }
    return OK;
}

Error NetwMultiplayer::sync_admit_frame(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    int64_t p_flags,
    int64_t p_tick,
    const PackedByteArray &p_payload
) const {
    NETW_ZONE_NC("sync admit frame", colors::WIRE);
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _sync_admit_frame,
            p_sender,
            p_route,
            p_comp,
            p_channel,
            p_flags,
            p_tick,
            p_payload,
            answered
        )) {
        return answered;
    }
    return sync_admit_frame_default(
        p_sender,
        p_route,
        p_comp,
        p_channel,
        p_flags,
        p_tick,
        p_payload
    );
}

void NetwMultiplayer::report_event(
    int64_t p_event,
    int64_t p_route,
    const Dictionary &p_detail,
    int64_t p_peer,
    const StringName &p_entity_id,
    const Dictionary &p_model,
    int64_t p_verdict
) {
    if (!event_wants(p_event, p_route)) {
        return;
    }
    event_emit(
        p_event,
        p_route,
        p_detail,
        p_entity_id,
        p_peer,
        p_verdict,
        p_model
    );
}

void NetwMultiplayer::table_publish_intake() {
    const TypedArray<RID> touched = table_core->touched_tables();
    for (int index = 0; index < touched.size(); index++) {
        table_publish(touched[index]);
    }
    table_core->begin_intake();
}

void NetwMultiplayer::table_publish(const RID &p_table) {
    emit_signal(SIG_TABLE_RECEIVED, p_table, table_core->tick_of(p_table));
}

Error NetwMultiplayer::table_admit_frame_default(
    int64_t p_sender,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    if (p_channel != gate_channels.table) {
        return ERR_INVALID_DATA;
    }
    if (p_sender != 1) {
        table_core->count_bad_sender();
        return ERR_UNAUTHORIZED;
    }
    if (p_payload.is_empty()) {
        return ERR_INVALID_DATA;
    }
    return table_core->admit_header(netw::table::Core::peek_header(p_payload));
}

Error NetwMultiplayer::table_admit_frame(
    int64_t p_sender,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("table admit frame", colors::TABLE);
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _table_admit_frame,
            p_sender,
            p_channel,
            p_payload,
            answered
        )) {
        return answered;
    }
    return table_admit_frame_default(p_sender, p_channel, p_payload);
}

Node *NetwMultiplayer::repl_comp_node(
    int64_t p_route,
    int64_t p_comp,
    const String &p_path
) {
    const int64_t state = liveness_route_state(p_route);
    const bool lingering = state == int64_t(NetwLivenessCore::STATE_LINGERING);
    if (state == int64_t(NetwLivenessCore::STATE_DEAD) || lingering) {
        repl_drops.not_live++;
        if (lingering) {
            repl_drops.lingering_route++;
        } else {
            repl_drops.dead_route++;
        }
        NETW_TRACE(
            sys::TRANSPORT,
            "dropped a frame for route %d, %s",
            int(p_route),
            lingering ? "lingering" : "dead"
        );
        return nullptr;
    }
    const Ref<NetwEntity> entity = wrapper_for_route(p_route);
    Node *root = entity.is_valid() ? entity->get_owner() : nullptr;
    if (root == nullptr) {
        repl_drops.no_node++;
        return nullptr;
    }
    NetwCompTable &table = entity->comp_table();
    Node *held = root;
    switch (table.classify(p_comp, p_path)) {
        case NetwCompTable::ADDRESS_HOSTILE:
            NETW_WARN(
                sys::TRANSPORT,
                "path traversal clamp rejected '%s' on '%s'",
                String(p_path).utf8().get_data(),
                String(root->get_name()).utf8().get_data()
            );
            repl_drops.traversal++;
            return nullptr;
        case NetwCompTable::ADDRESS_UNMAPPED:
            repl_drops.no_node++;
            return nullptr;
        case NetwCompTable::ADDRESS_RELATIVE:
            held = root->get_node_or_null(NodePath(p_path));
            if (held == nullptr) {
                repl_drops.comp_unresolved++;
                NETW_TRACE(
                    sys::TRANSPORT,
                    "component '%s' on '%s' is not resolved yet",
                    String(p_path).utf8().get_data(),
                    String(root->get_name()).utf8().get_data()
                );
                return nullptr;
            }
            if (held != root && !root->is_ancestor_of(held)) {
                NETW_WARN(
                    sys::TRANSPORT,
                    "component path '%s' escaped the entity subtree on '%s'",
                    String(p_path).utf8().get_data(),
                    String(root->get_name()).utf8().get_data()
                );
                repl_drops.traversal++;
                return nullptr;
            }
            break;
        case NetwCompTable::ADDRESS_MAPPED:
            held = root->get_node_or_null(NodePath(table.path_for_id(p_comp)));
            break;
        default:
            break;
    }
    if (held == nullptr) {
        repl_drops.no_node++;
        return nullptr;
    }
    return held;
}

void NetwMultiplayer::repl_note_unknown_route() {
    repl_drops.unknown_route++;
}

void NetwMultiplayer::repl_note_gate_verdict(int64_t p_verdict) {
    if (p_verdict == int64_t(ERR_DOES_NOT_EXIST)) {
        repl_drops.unknown_route++;
    } else if (p_verdict == int64_t(ERR_SKIP)) {
        repl_drops.not_live++;
    } else if (p_verdict == int64_t(ERR_UNAVAILABLE)) {
        repl_drops.no_node++;
    }
}

Dictionary NetwMultiplayer::repl_drop_stats() const {
    Dictionary out;
    out[StringName("drops_unknown_route")] = repl_drops.unknown_route;
    out[StringName("drops_not_live")] = repl_drops.not_live;
    out[StringName("drops_lingering_route")] = repl_drops.lingering_route;
    out[StringName("drops_dead_route")] = repl_drops.dead_route;
    out[StringName("drops_no_node")] = repl_drops.no_node;
    out[StringName("drops_traversal")] = repl_drops.traversal;
    out[StringName("drops_comp_unresolved")] = repl_drops.comp_unresolved;
    return out;
}

LocalVector<repl::RowOffer> NetwMultiplayer::sync_pump_offers(
    NetwSyncModel *p_model,
    ReplicationSend *p_send,
    const Array &p_bindings,
    int64_t p_tick,
    const Callable &p_bind,
    const Callable &p_tap
) {
    NETW_ZONE_NC("sync pump offers", colors::WIRE);
    NETW_ZONE_VALUE(p_bindings.size());
    LocalVector<repl::RowOffer> offers;
    if (p_model == nullptr) {
        return offers;
    }
    for (int at = 0; at < p_bindings.size(); at++) {
        const Ref<NetwPropertySetBinding> binding = p_bindings[at];
        if (binding.is_null()) {
            continue;
        }
        Node *node = binding->node();
        if (node == nullptr) {
            sync_flush.skips_invalid_node++;
            continue;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        if (entity.is_null()) {
            sync_flush.skips_no_entity++;
            continue;
        }
        if (binding->get_route() <= 0 && p_bind.is_valid()) {
            p_bind.call(binding);
        }
        const int64_t route = binding->get_route();
        if (route <= 0) {
            sync_flush.skips_no_route++;
            continue;
        }
        const repl::SetRow *row = p_model->row_for(
            route,
            int64_t(NetwSyncModel::KIND_DERIVED),
            binding->get_order_key(),
            binding->get_set().is_valid() ? binding->get_set()->record : 0
        );
        if (row == nullptr) {
            sync_flush.skips_no_route++;
            continue;
        }
        const int64_t ordinal = row->ordinal;
        const PackedInt32Array recipients = p_model->offer_row(
            route,
            ordinal,
            int64_t(get_unique_id()),
            node->is_inside_tree() && node->is_multiplayer_authority(),
            entity->get_controller(),
            rpc_get_recipients(entity)
        );
        if (recipients.is_empty()) {
            continue;
        }
        if (p_tap.is_valid()) {
            p_tap.call(
                binding,
                route,
                binding->get_authored_tick() >= 0 ? binding->get_authored_tick()
                                                  : p_tick
            );
        }
        binding->offer_rows(
            ordinal,
            recipients,
            p_tick,
            liveness_route_epoch(route),
            offers
        );
        if (p_send != nullptr) {
            p_send->retain_row(route, ordinal, recipients);
        }
    }
    return offers;
}

void NetwMultiplayer::sync_flush_offers(
    ReplicationSend *p_send,
    const LocalVector<repl::RowOffer> &p_offers,
    int64_t p_row_channel,
    int64_t p_window_channel,
    int64_t p_delta_channel
) {
    if (p_send == nullptr || p_offers.is_empty()) {
        return;
    }
    const repl::SessionResult result
        = p_send->run(p_offers, datagram_budget() * 8, datagram_base_tick());
    if (event_wants(int64_t(EventPlane::GATHER), 0)) {
        Dictionary detail;
        detail[StringName("offers")] = int64_t(p_offers.size());
        detail[StringName("sends")] = int64_t(result.sends.size());
        event_emit(
            int64_t(EventPlane::GATHER),
            0,
            detail,
            StringName(),
            0,
            int64_t(OK),
            Dictionary()
        );
    }
    const bool grain = attribution_is_armed();
    for (uint32_t at = 0; at < result.sends.size(); at++) {
        const repl::RowSend &send = result.sends[at];
        if (grain) {
            sync_note_columns(p_offers[send.offer], send.mask);
        }
        send_to(
            int64_t(send.peer),
            send.route,
            send.reliable ? p_delta_channel
                          : (send.windowed ? p_window_channel : p_row_channel),
            send.bytes,
            send.reliable,
            int64_t(send.comp),
            String(),
            true
        );
        p_send->confirm(send);
        if (send.reliable) {
            sync_flush.retained++;
            continue;
        }
        if (send.windowed) {
            sync_flush.windows++;
            sync_flush.window_samples += int64_t(send.sample_count);
            continue;
        }
        sync_flush.rows++;
        const wire::WirePlan plan
            = wire::WirePlan::compile(p_offers[send.offer].declared());
        if (send.mask == plan.full_mask()) {
            sync_flush.whole_rows++;
        }
    }
    sync_flush.staged_out += int64_t(result.staged_out);
    sync_flush.ungathered += int64_t(result.ungathered);
    sync_flush.deferred += int64_t(result.deferred);
}

void NetwMultiplayer::sync_note_columns(
    const repl::RowOffer &p_offer,
    uint64_t p_mask
) {
    const wire::WirePlan plan = wire::WirePlan::compile(p_offer.declared());
    const int64_t schema = int64_t(p_offer.declared().shape_hash);
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        if ((p_mask & (uint64_t(1) << index)) == 0) {
            continue;
        }
        const wire::ColumnPlan &slot = plan.column(index);
        attribution_note_column(
            schema,
            int64_t(index),
            int64_t(slot.stride) * int64_t(slot.width)
        );
    }
}

Dictionary NetwMultiplayer::peer_link_stats(int64_t p_peer) const {
    Dictionary out;
    const ClockEngine &clock = clock_engine();
    out[StringName("rtt")] = clock.rtt_avg();
    out[StringName("jitter")] = clock.rtt_jitter();
    out[StringName("reorders")] = seq_book.reorder_count(p_peer);
    out[StringName("duplicates")] = seq_book.duplicate_count(p_peer);
    out[StringName("loss")] = 0.0;
    out[StringName("mode")] = String("good");
    out[StringName("budget_bits")] = datagram_budget() * 8;

    ReplicationCore *plane = get_replication_plane();
    SyncPipeline *pipeline
        = plane != nullptr ? plane->get_sync_pipeline() : nullptr;
    ReplicationSend *send
        = pipeline != nullptr ? pipeline->row_send() : nullptr;
    if (send == nullptr) {
        return out;
    }
    out[StringName("loss")] = double(send->link_loss(p_peer));
    out[StringName("mode")] = String(
        send->link_mode(p_peer) == repl::LinkGovernor::BAD ? "bad" : "good"
    );
    out[StringName("budget_bits")]
        = send->link_budget_bits(p_peer, datagram_budget() * 8);
    return out;
}

Dictionary NetwMultiplayer::sync_explain(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_peer
) const {
    Dictionary out;
    ReplicationCore *plane = get_replication_plane();
    SyncPipeline *pipeline
        = plane != nullptr ? plane->get_sync_pipeline() : nullptr;
    ReplicationSend *send
        = pipeline != nullptr ? pipeline->row_send() : nullptr;
    if (send == nullptr) {
        return out;
    }
    const repl::RowExplain held = send->explain(p_route, p_comp, p_peer);
    static const char *const NAMES[]
        = {"unoffered",
           "sent",
           "deferred",
           "caught_up",
           "ungathered",
           "refused"};
    out[StringName("verdict")] = String(NAMES[int(held.verdict)]);
    out[StringName("tick")] = held.tick;
    out[StringName("sticky")] = int64_t(held.sticky);
    out[StringName("in_flight")] = int64_t(held.in_flight);
    out[StringName("has_baseline")] = held.has_baseline;
    return out;
}

Dictionary NetwMultiplayer::sync_flush_stats() const {
    Dictionary out;
    out[StringName("row_frames_out")] = sync_flush.rows;
    out[StringName("row_frames_full")] = sync_flush.whole_rows;
    out[StringName("retained_frames_out")] = sync_flush.retained;
    out[StringName("window_frames_out")] = sync_flush.windows;
    out[StringName("window_samples_out")] = sync_flush.window_samples;
    out[StringName("row_frames_stage_refused")] = sync_flush.staged_out;
    out[StringName("row_frames_ungathered")] = sync_flush.ungathered;
    out[StringName("row_frames_deferred")] = sync_flush.deferred;
    out[StringName("pump_skips_invalid_node")] = sync_flush.skips_invalid_node;
    out[StringName("pump_skips_no_entity")] = sync_flush.skips_no_entity;
    out[StringName("pump_skips_no_route")] = sync_flush.skips_no_route;
    return out;
}

Error NetwMultiplayer::display_lane(
    const RID &p_entity,
    const StringName &p_track,
    const Variant &p_value
) {
    ReplicationCore *plane = (get_replication_plane());
    Object *seam = plane != nullptr
        ? plane->gate_seam(StringName("_display_write"))
        : nullptr;
    const Error verdict = seam != nullptr
        ? Error(int(seam->call("_display_write", p_entity, p_track, p_value)))
        : display_write(p_entity, p_track, p_value);

    Dictionary detail;
    detail[StringName("track")] = p_track;
    report_event(
        EventPlane::DISPLAY_WRITE,
        liveness_core->route_of(p_entity),
        detail,
        0,
        StringName(),
        Dictionary(),
        int64_t(verdict)
    );
    return verdict;
}

Array NetwMultiplayer::sync_gather_set_default(const RID &, int64_t) {
    return staged_gatherer.is_valid() ? Array(staged_gatherer.call()) : Array();
}

Array NetwMultiplayer::gather_set(const RID &p_entity, int64_t p_comp) {
    Array answered;
    if (GDVIRTUAL_CALL(_sync_gather_set, p_entity, p_comp, answered)) {
        return answered;
    }
    return sync_gather_set_default(p_entity, p_comp);
}

Error NetwMultiplayer::sync_apply_set_default(
    const RID &,
    int64_t,
    const Array &p_values
) {
    return staged_applier.is_valid() ? Error(int(staged_applier.call(p_values)))
                                     : ERR_UNCONFIGURED;
}

Error NetwMultiplayer::apply_set(
    const RID &p_entity,
    int64_t p_comp,
    const Array &p_values
) {
    Error answered = OK;
    if (GDVIRTUAL_CALL(_sync_apply_set, p_entity, p_comp, p_values, answered)) {
        return answered;
    }
    return sync_apply_set_default(p_entity, p_comp, p_values);
}

Array NetwMultiplayer::run_gather_set(
    const RID &p_entity,
    int64_t p_comp,
    const Callable &p_gatherer
) {
    NETW_ZONE_NC("session gather set seam", colors::WIRE);
    staged_gatherer = p_gatherer;
    ReplicationCore *plane = (get_replication_plane());
    Object *seam = plane != nullptr
        ? plane->gate_seam(StringName("_sync_gather_set"))
        : nullptr;
    const Array values = seam != nullptr
        ? Array(seam->call("_sync_gather_set", p_entity, p_comp))
        : gather_set(p_entity, p_comp);
    staged_gatherer = Callable();
    return values;
}

Error NetwMultiplayer::run_apply_set(
    const RID &p_entity,
    int64_t p_comp,
    const Array &p_values,
    const Callable &p_applier
) {
    NETW_ZONE_NC("session apply set seam", colors::WIRE);
    staged_applier = p_applier;
    ReplicationCore *plane = (get_replication_plane());
    Object *seam = plane != nullptr
        ? plane->gate_seam(StringName("_sync_apply_set"))
        : nullptr;
    const Error verdict = seam != nullptr
        ? Error(int(seam->call("_sync_apply_set", p_entity, p_comp, p_values)))
        : apply_set(p_entity, p_comp, p_values);
    staged_applier = Callable();
    return verdict;
}

void NetwMultiplayer::display_resolve_role(display::Runtime *p_runtime) {
    if (p_runtime != nullptr) {
        display::resolve_role(p_runtime, display_hooks);
    }
}

bool NetwMultiplayer::display_wants_runtime(Node *p_owner) const {
    return display::wants_runtime(p_owner, display_hooks);
}

void NetwMultiplayer::display_rebuild_runtime(display::Runtime *p_runtime) {
    display::rebuild_runtime(p_runtime, display_hooks);
}

display::Channel *NetwMultiplayer::display_ensure_state(
    display::Runtime *p_runtime,
    Node *p_node,
    const StringName &p_source_prop,
    const StringName &p_target_prop,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick
) {
    if (p_runtime == nullptr) {
        return nullptr;
    }
    return display::ensure_state(
        p_runtime,
        p_node,
        p_source_prop,
        p_target_prop,
        p_spec,
        p_authoring_tick,
        display_hooks
    );
}

static Ref<NetwPredictionHandle> prediction_of(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Ref<NetwPredictionHandle>();
    }
    return p_entity->get_prediction();
}

int NetwMultiplayer::display_default_role(
    const RID &p_entity,
    bool p_authors_streams
) {
    display::Runtime *p_runtime = display_book->runtime_of(p_entity);
    if (p_runtime == nullptr) {
        return netw::display::ROLE_DISABLED;
    }
    display::RoleFacts facts;
    Node *owner = p_runtime->owner();
    facts.owner_is_authority = owner != nullptr && owner->is_inside_tree()
        && owner->is_multiplayer_authority();
    const Ref<NetwEntity> entity = p_runtime->entity();
    if (entity.is_null()) {
        return display::resolve_role_facts(facts);
    }
    const Ref<NetwPredictionHandle> handle = prediction_of(entity);
    facts.authors_streams = p_authors_streams;
    facts.controlled_locally = entity->get_is_controlled_locally();
    facts.predicted_input = handle.is_valid()
        && handle->get_input_source() == NetwPredict::INPUT_SOURCE_PREDICTED;
    facts.prediction_registered = handle.is_valid() && handle->is_registered();
    facts.simulates_locally = handle.is_valid() && handle->is_registered()
        ? handle->get_sim_mode() != NetwPredict::SIM_MODE_DISPLAY
        : authoring::declares_prediction(p_runtime->owner());
    return display::resolve_role_facts(facts);
}

double NetwMultiplayer::display_default_chase_clamp(const RID &p_entity) {
    display::Runtime *p_runtime = display_book->runtime_of(p_entity);
    if (p_runtime == nullptr) {
        return INFINITY;
    }
    const Ref<NetwPredictionHandle> handle = prediction_of(p_runtime->entity());
    if (handle.is_null()) {
        return INFINITY;
    }
    return MAX(handle->get_teleport_threshold(), 0.0);
}

void NetwMultiplayer::display_on_recovered(
    int64_t,
    const Dictionary &p_deltas,
    bool p_teleported,
    int64_t,
    const RID &p_entity
) {
    display_absorb_recovery(
        display_book->runtime_of(p_entity),
        p_deltas,
        p_teleported
    );
}

void NetwMultiplayer::display_default_chase_hook(
    const RID &p_entity,
    bool p_bind
) {
    static const StringName RECOVERED("recovered");
    display::Runtime *p_runtime = display_book->runtime_of(p_entity);
    if (p_runtime == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = p_runtime->entity();
    const Ref<NetwPredictionHandle> handle = prediction_of(entity);

    Array hooks = p_runtime->get_chase_hooks();
    for (int at = 0; at < hooks.size(); at++) {
        const Callable held = hooks[at];
        if (handle.is_valid() && handle->is_connected(RECOVERED, held)) {
            handle->disconnect(RECOVERED, held);
        }
    }
    hooks.clear();
    if (!p_bind || handle.is_null()) {
        p_runtime->set_chase_hooks(hooks);
        return;
    }
    const Callable hook
        = callable_mp(this, &NetwMultiplayer::display_on_recovered)
              .bind(p_entity);
    handle->connect(RECOVERED, hook);
    hooks.push_back(hook);
    p_runtime->set_chase_hooks(hooks);
}

static bool runtime_tracks_key(
    display::Runtime *p_runtime,
    const StringName &p_key
) {
    for (const display::Channel *state : p_runtime->channels()) {
        if (state->get_source_prop() == p_key || state->get_name() == p_key) {
            return true;
        }
    }
    return false;
}

static bool sync_feeds_runtime(
    display::Runtime *p_runtime,
    MultiplayerSynchronizer *p_sync
) {
    const Ref<SceneReplicationConfig> config = p_sync->get_replication_config();
    if (config.is_null()) {
        return false;
    }
    const TypedArray<NodePath> held = config->get_properties();
    for (int at = 0; at < held.size(); at++) {
        const NodePath path = held[at];
        const int names = path.get_subname_count();
        if (names == 0) {
            continue;
        }
        if (runtime_tracks_key(p_runtime, path.get_subname(names - 1))) {
            return true;
        }
    }
    return false;
}

static bool set_feeds_runtime(
    display::Runtime *p_runtime,
    const Ref<NetwPropertySet> &p_set
) {
    const TypedArray<NetwPropertySetColumn> columns = p_set->get_columns();
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column.is_valid()
            && runtime_tracks_key(p_runtime, column->get_key())) {
            return true;
        }
    }
    return false;
}

void NetwMultiplayer::display_compute_sync_intervals(const RID &p_entity) {
    display::Runtime *p_runtime = display_book->runtime_of(p_entity);
    if (p_runtime == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = p_runtime->entity();
    if (entity.is_null()) {
        return;
    }
    p_runtime->set_authoring_binding(Ref<RefCounted>());

    double widest = 0.0;
    const TypedArray<MultiplayerSynchronizer> held = entity->synchronizers();
    for (int at = 0; at < held.size(); at++) {
        MultiplayerSynchronizer *sync
            = Object::cast_to<MultiplayerSynchronizer>(held[at]);
        if (sync == nullptr || !sync->is_visibility_public()
            || !sync_feeds_runtime(p_runtime, sync)) {
            continue;
        }
        widest = MAX(
            widest,
            MAX(sync->get_replication_interval(), sync->get_delta_interval())
        );
    }

    const Ref<NetwPropertySetBinding> state = entity->get_state_binding();
    if (state.is_valid() && set_feeds_runtime(p_runtime, state->get_set())) {
        p_runtime->set_authoring_binding(state);
    }
    if (widest <= 0.0 || !clock_engine().get_configured()) {
        return;
    }
    p_runtime->display_playhead().set_expected_interval_ticks(
        MAX(1, int(Math::ceil(widest * clock_engine().get_tickrate())))
    );
}

bool NetwMultiplayer::display_default_authors_streams(const RID &p_entity) {
    display::Runtime *p_runtime = display_book->runtime_of(p_entity);
    if (p_runtime == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = p_runtime->entity();
    if (entity.is_null()) {
        return false;
    }
    bool found = false;
    const TypedArray<MultiplayerSynchronizer> held = entity->synchronizers();
    for (int at = 0; at < held.size(); at++) {
        MultiplayerSynchronizer *sync
            = Object::cast_to<MultiplayerSynchronizer>(held[at]);
        if (sync == nullptr || !sync->is_inside_tree()
            || sync->get_replication_config().is_null()
            || !sync->is_visibility_public()
            || !sync_feeds_runtime(p_runtime, sync)) {
            continue;
        }
        if (!sync->is_multiplayer_authority()) {
            return false;
        }
        found = true;
    }

    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr) {
        return found;
    }
    const TypedArray<NetwPropertySetBinding> derived
        = plane->derived_group(p_runtime->get_route());
    for (int at = 0; at < derived.size(); at++) {
        const Ref<NetwPropertySetBinding> binding = derived[at];
        if (binding.is_null()) {
            continue;
        }
        Node *node = binding->node();
        if (node == nullptr || !node->is_inside_tree()) {
            continue;
        }
        const Ref<NetwPropertySet> set = binding->get_set();
        if (set->get_audience() != NetwPropertySet::AUDIENCE_PUBLIC
            || !set_feeds_runtime(p_runtime, set)) {
            continue;
        }
        const bool authored = set->get_record() == NetwPropertySet::RECORD_STATE
            ? get_unique_id() == 1
            : netw::entity::Control::policy_admits(
                  set->get_policy(),
                  get_unique_id(),
                  node->get_multiplayer_authority(),
                  entity->get_controller()
              );
        if (!authored) {
            return false;
        }
        found = true;
    }
    return found;
}

void NetwMultiplayer::display_pump_runtime(
    display::Runtime *p_runtime,
    const display::Timing &p_timing,
    display::PumpStats &p_stats
) {
    if (p_runtime == nullptr) {
        return;
    }
    display::pump_runtime(p_runtime, p_timing, p_stats, display_hooks);
}

double NetwMultiplayer::display_chase_smooth_time(
    display::Runtime *p_runtime,
    const display::Timing &p_timing
) const {
    if (p_runtime == nullptr) {
        return 0.0;
    }
    return display::chase_smooth_time(p_runtime, p_timing);
}

Error NetwMultiplayer::display_pump_entity(
    const RID &p_entity,
    const display::Timing &p_timing
) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    NETW_ERR_COND_V(
        runtime == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::INTERPOLATION,
        "no display runtime for entity %d",
        int(p_entity.get_id())
    );
    display_pump_runtime(runtime, p_timing, display_book->get_stats());
    return OK;
}

void NetwMultiplayer::display_absorb_recovery(
    display::Runtime *p_runtime,
    const Dictionary &p_deltas,
    bool p_teleported
) {
    if (p_runtime == nullptr) {
        return;
    }
    display::absorb_recovery(p_runtime, p_deltas, p_teleported, display_hooks);
}

bool NetwMultiplayer::persistence_serves() {
    return !has_multiplayer_peer() || is_server();
}

void NetwMultiplayer::persistence_arm_quit_guard() {
    if (persistence.quit_guard.is_valid()) {
        persistence.quit_guard.call();
        return;
    }
    SceneTree *tree = gd::scene_tree();
    if (is_host() && tree != nullptr) {
        tree->set_auto_accept_quit(false);
    }
}

Ref<NetwPersistenceEngine> NetwMultiplayer::persistence_engine_for(
    NetwEntity *p_entity
) {
    Ref<NetwEntity> entity = p_entity;
    if (entity.is_null()) {
        return Ref<NetwPersistenceEngine>();
    }
    Node *owner = entity->get_owner();
    if (owner == nullptr) {
        return Ref<NetwPersistenceEngine>();
    }
    const RID handle = entity->get_rid_handle();
    const Ref<NetwPersistenceEngine> enrolled
        = persistence.engines.engine_of(handle);
    if (enrolled.is_valid()) {
        return enrolled;
    }
    const Dictionary declared = NetwPersistenceEngine::config_of(owner);
    if (declared.is_empty()) {
        return Ref<NetwPersistenceEngine>();
    }
    const Ref<NetwPersistenceEngine> engine
        = NetwPersistenceEngine::create(entity.ptr(), declared);
    if (engine.is_null()) {
        return engine;
    }
    if (engine->columns_empty()) {
        NETW_WARN(
            sys::TABLE,
            "configure_persistence on '%s' declares no persisted field, so it "
            "saves nothing: mark one with configure_property(...).persisted()",
            String(owner->get_name())
        );
    }
    persistence.engines.enroll(handle, engine);
    engine->lint();
    if (persistence_serves()) {
        persistence_arm_quit_guard();
    }
    return engine;
}

void NetwMultiplayer::persist_pump(double p_delta) {
    if (GDVIRTUAL_CALL(_persist_tick, p_delta)) {
        return;
    }
    persist_tick_default(p_delta);
}

void NetwMultiplayer::persist_tick_default(double p_delta) {
    persist::snapshot_tick(persistence.engines, p_delta, persistence_serves());
}

void NetwMultiplayer::persistence_flush_all() {
    persist::flush_all(persistence.engines, persistence_serves());
}

TypedArray<NetwPersistenceEngine> NetwMultiplayer::persistence_live_engines() {
    TypedArray<NetwPersistenceEngine> live;
    const TypedArray<RID> entities = persistence.engines.entities();
    for (int at = 0; at < entities.size(); ++at) {
        const Ref<NetwPersistenceEngine> engine
            = persistence.engines.engine_of(entities[at]);
        if (engine.is_valid() && engine->owner_node() != nullptr) {
            live.push_back(engine);
        }
    }
    return live;
}

void NetwMultiplayer::persist_shutdown() {
    if (persistence.shutting_down || !persistence_serves()) {
        return;
    }
    persistence.shutting_down = true;
    NETW_TRACE(
        sys::TABLE,
        "persistence shutdown draining %d enrolled engine(s)",
        persistence.engines.size()
    );
    if (persistence.drain.is_valid()) {
        persistence.drain.call();
        return;
    }
    persistence_drain_start(SHUTDOWN_NOTIFY_DELAY);
}

bool NetwMultiplayer::counts_verdict(Error p_verdict) {
    return GateVerdictBook::counts(p_verdict);
}

int64_t NetwMultiplayer::stats_get_verdict_count(Error p_verdict) const {
    return verdict_book.total(p_verdict);
}

bool NetwMultiplayer::claim_verdict_warning(Error p_verdict, int64_t p_route) {
    return verdict_book.claim_warning(p_verdict, p_route);
}

bool NetwMultiplayer::warn_verdict(Error p_verdict, int64_t p_route) {
    count_verdict(p_verdict, p_route);
    return claim_verdict_warning(p_verdict, p_route);
}

Error NetwMultiplayer::sink_verdict(Error p_verdict, int64_t p_route) {
    switch (GateVerdictBook::report_of(p_verdict)) {
        case GateVerdictBook::QUIET:
            break;
        case GateVerdictBook::WARN:
            if (verdict_book.claim_warning(p_verdict, p_route)) {
                NETW_WARN(
                    sys::TRANSPORT,
                    "refused carrier input with error %d on route %d",
                    int(p_verdict),
                    int(p_route)
                );
            }
            break;
        case GateVerdictBook::DEFECT:
            NETW_ERROR(
                sys::TRANSPORT,
                "a carrier sink answered %d, which is not a verdict",
                int(p_verdict)
            );
            break;
    }
    if (p_verdict != OK) {
        count_verdict(p_verdict, p_route);
    }
    return p_verdict;
}

Error NetwMultiplayer::finish_stage_verdict(
    int64_t p_stage,
    Error p_verdict,
    int64_t p_route
) {
    if (plane.wants(p_stage, p_route)) {
        EventPlane::Emission fact(
            p_stage,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = p_route;
        fact.verdict = p_verdict;
        plane.emit(fact);
    }
    return sink_verdict(p_verdict, p_route);
}

void NetwMultiplayer::clear_verdicts() {
    verdict_book.clear();
}

RID NetwMultiplayer::schema_create(const StringName &p_name) {
    if (p_name == StringName()) {
        return RID();
    }
    const RID existing = schema_core.find(p_name);
    if (existing.is_valid()) {
        schema_core.declare(existing, p_name);
        return existing;
    }
    const RID minted = schemas.rid_create();
    schema_core.declare(minted, p_name);
    return minted;
}

int NetwMultiplayer::schema_add_column(
    const RID &p_schema,
    const StringName &p_key,
    ColumnType p_type,
    int p_stride
) {
    return schema_core.add_column(p_schema, p_key, p_type, p_stride);
}

void NetwMultiplayer::schema_set_column_quantizer(
    const RID &p_schema,
    int p_column,
    const Ref<NetwQuantize> &p_quantizer
) {
    schema_core.set_column_quantizer(p_schema, p_column, p_quantizer);
}

Error NetwMultiplayer::schema_seal(const RID &p_schema) {
    return schema_core.seal(p_schema);
}

RID NetwMultiplayer::schema_find(const StringName &p_name) const {
    return schema_core.find(p_name);
}

RID NetwMultiplayer::schema_find_or_adopt(const StringName &p_name) {
    const RID found = schema_find(p_name);
    if (found.is_valid() || schema_model::find(p_name).is_null()) {
        return found;
    }
    adopt_schema_declarations();
    return schema_find(p_name);
}

RID NetwMultiplayer::table_find_or_adopt(const StringName &p_name) {
    const RID found = table_find(p_name);
    if (found.is_valid() || schema_model::find(p_name).is_null()) {
        return found;
    }
    adopt_schema_declarations();
    adopt_table_declarations();
    return table_find(p_name);
}

StringName NetwMultiplayer::schema_get_name(const RID &p_schema) const {
    return schema_core.name_of(p_schema);
}

Variant::Type NetwMultiplayer::schema_get_element_type(ColumnType p_type) {
    return Variant::Type(SchemaCore::element_type(p_type));
}

void NetwMultiplayer::adopt_schema_declarations() {
    const TypedArray<NetwSchema> declared = schema_model::declarations();
    for (int at = 0; at < declared.size(); ++at) {
        const Ref<NetwSchema> declaration = declared[at];
        if (declaration.is_null()) {
            continue;
        }
        const RID schema = schema_create(declaration->get_schema_name());
        if (!schema.is_valid()) {
            continue;
        }
        const TypedArray<NetwSchemaColumn> columns = declaration->get_columns();
        for (int at_column = 0; at_column < columns.size(); ++at_column) {
            const Ref<NetwSchemaColumn> column = columns[at_column];
            if (column.is_null()) {
                continue;
            }
            const int index = schema_add_column(
                schema,
                column->get_key(),
                static_cast<ColumnType>(column->get_type()),
                column->get_stride()
            );
            if (index >= 0) {
                schema_set_column_quantizer(
                    schema,
                    index,
                    column->get_quantizer()
                );
            }
        }
        if (schema_seal(schema) != OK) {
            NETW_ERROR(
                sys::TABLE,
                "schema '%s' was redeclared with a different shape; peers "
                "built from the two declarations cannot read each other",
                String(declaration->get_schema_name())
            );
        }
    }
}

void NetwMultiplayer::adopt_table_declarations() {
    const TypedArray<NetwSchema> declared = schema_model::declarations();
    for (int at = 0; at < declared.size(); ++at) {
        const Ref<NetwSchema> declaration = declared[at];
        if (declaration.is_null() || !declaration->is_replicated()) {
            continue;
        }
        const RID schema = schema_core.find(declaration->get_schema_name());
        if (!schema.is_valid()) {
            continue;
        }
        const RID table = table_create(schema);
        if (!table.is_valid()) {
            NETW_ERROR(
                sys::TABLE,
                "schema '%s' asks for a table but declares a VARIANT column; "
                "variable width has no row budget. Send it through a channel "
                "or drop replicated() from the schema",
                String(declaration->get_schema_name())
            );
            continue;
        }
        table_set_param(
            table,
            TABLE_PARAM_RELIABLE,
            declaration->is_reliable()
        );
    }
}

int NetwMultiplayer::schema_get_hash(const RID &p_schema) const {
    return schema_core.hash_of(p_schema);
}

int NetwMultiplayer::schema_get_column_count(const RID &p_schema) const {
    return schema_core.column_count(p_schema);
}

StringName NetwMultiplayer::schema_get_column_key(
    const RID &p_schema,
    int p_column
) const {
    return schema_core.column_key(p_schema, p_column);
}

NetwMultiplayer::ColumnType NetwMultiplayer::schema_get_column_type(
    const RID &p_schema,
    int p_column
) const {
    return ColumnType(schema_core.column_type(p_schema, p_column));
}

int NetwMultiplayer::schema_get_column_stride(
    const RID &p_schema,
    int p_column
) const {
    return schema_core.column_stride(p_schema, p_column);
}

RID NetwMultiplayer::table_create(const RID &p_schema) {
    const SchemaRecord *record = schema_core.record_of(p_schema);
    if (record == nullptr || !record->sealed) {
        return RID();
    }
    const RID *bound = table_by_name.getptr(record->name);
    if (bound && bound->is_valid()) {
        return *bound;
    }
    const RID minted = tables.rid_create();
    if (table_core->declare(minted, record) != OK) {
        return RID();
    }
    table_by_name[record->name] = minted;
    table_schema[int64_t(minted.get_id())] = p_schema;
    return minted;
}

RID NetwMultiplayer::table_get_schema(const RID &p_table) const {
    const RID *bound = table_schema.getptr(int64_t(p_table.get_id()));
    return bound ? *bound : RID();
}

void NetwMultiplayer::table_set_param(
    const RID &p_table,
    TableParam p_param,
    const Variant &p_value
) {
    if (p_param == TABLE_PARAM_RELIABLE) {
        table_core->set_reliable(p_table, bool(p_value));
    }
}

RID NetwMultiplayer::table_find(const StringName &p_name) const {
    const RID *bound = table_by_name.getptr(p_name);
    return bound ? *bound : RID();
}

int NetwMultiplayer::table_get_wire_hash(const RID &p_table) const {
    return table_core->schema_hash(p_table);
}

Error NetwMultiplayer::table_write_routes(
    const RID &p_table,
    const PackedInt64Array &p_routes
) {
    return table_core->write_routes(p_table, p_routes);
}

Error NetwMultiplayer::table_write_column(
    const RID &p_table,
    int p_column,
    const Variant &p_data
) {
    return table_core->write_column(p_table, p_column, p_data);
}

Dictionary NetwMultiplayer::persist_table_commit(
    const RID &p_table,
    const RID &p_schema,
    const Dictionary &p_data
) {
    Dictionary out;
    out[StringName("routes")] = PackedInt64Array();
    out[StringName("ids")] = PackedStringArray();
    if (p_data.is_empty()) {
        return out;
    }

    const PackedStringArray ids
        = PackedStringArray(p_data.get(StringName("ids"), PackedStringArray()));
    const PackedInt64Array routes = liveness_claim_routes(ids.size());
    table_write_routes(p_table, routes);
    const int columns = schema_get_column_count(p_schema);
    for (int column = 0; column < columns; ++column) {
        const StringName key = schema_get_column_key(p_schema, column);
        const int type = schema_get_column_type(p_schema, column);
        const int stride = schema_get_column_stride(p_schema, column);
        const int wanted = SchemaCore::storage_type(type);
        Variant stored = p_data.get(key, Variant());
        const int64_t expected = int64_t(routes.size()) * int64_t(stride);
        const bool usable = type != SchemaCore::ENTITY
            && int(stored.get_type()) == wanted
            && int64_t(stored.call(StringName("size"))) == expected;
        if (!usable) {
            stored = SchemaCore::make_storage(type);
            stored.call(StringName("resize"), expected);
        }
        table_write_column(p_table, column, stored);
    }
    table_commit(p_table);

    out[StringName("routes")] = routes;
    out[StringName("ids")] = ids;
    return out;
}

Error NetwMultiplayer::table_commit(const RID &p_table) {
    const int64_t tick = clock_engine().get_tick();
    const Error verdict = table_core->commit(p_table, tick);
    if (plane.wants(EventPlane::TABLE_COMMIT, 0)) {
        EventPlane::Emission fact(
            EventPlane::TABLE_COMMIT,
            EventPlane::AFTER,
            tick
        );
        fact.verdict = verdict;
        Dictionary detail;
        detail["stage"] = "table";
        detail["rows"] = table_read_routes(p_table).size();
        fact.detail = detail;
        plane.emit(fact);
    }
    return verdict;
}

PackedInt64Array NetwMultiplayer::table_read_routes(const RID &p_table) const {
    return table_core->read_routes(p_table);
}

Variant NetwMultiplayer::table_read_column(
    const RID &p_table,
    int p_column
) const {
    return table_core->read_column(p_table, p_column);
}

PackedInt64Array NetwMultiplayer::table_read_births(const RID &p_table) const {
    return table_core->read_births(p_table);
}

PackedInt64Array NetwMultiplayer::table_read_deaths(const RID &p_table) const {
    return table_core->read_deaths(p_table);
}

int NetwMultiplayer::table_get_row(const RID &p_table, int64_t p_route) const {
    return table_core->row_of(p_table, p_route);
}

PackedInt32Array NetwMultiplayer::table_get_rows(
    const RID &p_table,
    const PackedInt64Array &p_routes
) const {
    return table_core->rows_of(p_table, p_routes);
}

int64_t NetwMultiplayer::table_get_tick(const RID &p_table) const {
    return table_core->tick_of(p_table);
}

void NetwMultiplayer::lagcomp_effect_arm(
    const StringName &p_key,
    const Callable &p_revert,
    int p_timeout_ticks
) {
    const int ttl
        = p_timeout_ticks > 0 ? p_timeout_ticks : EFFECT_TIMEOUT_TICKS;
    effects.arm(p_key, p_revert, clock_engine().get_tick() + ttl);
}

bool NetwMultiplayer::lagcomp_effect_watch(
    const StringName &p_key,
    const Callable &p_confirmed,
    const Callable &p_denied
) {
    const bool watching = effects.watch(p_key, p_confirmed, p_denied);
    NETW_WARN_COND(
        !watching,
        sys::PREDICTION,
        "an effect was watched before it was armed, so '%s' has no revert to "
        "confirm or deny",
        p_key
    );
    return watching;
}

StringName NetwMultiplayer::lagcomp_effect_key(
    const RID &p_entity,
    int64_t p_tick,
    int64_t p_slot
) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    const StringName named
        = wrapper.is_valid() ? wrapper->get_entity_id() : StringName();
    if (named.is_empty()) {
        Array act_args;
        act_args.push_back(p_tick);
        act_args.push_back(p_slot);
        return StringName(String("act__{0}__{1}").format(act_args));
    }
    Array act_args;
    act_args.push_back(named);
    act_args.push_back(p_tick);
    act_args.push_back(p_slot);
    return StringName(String("act__{0}__{1}__{2}").format(act_args));
}

void NetwMultiplayer::write_display_param(
    const Ref<NetwEntity> &p_wrapper,
    int p_param,
    const Variant &p_value
) {
    if (p_wrapper.is_null()) {
        return;
    }
    const Ref<NetwDisplayHandle> handle = p_wrapper->get_interpolation();
    if (handle.is_null()) {
        return;
    }
    display::Decl *fallback = handle->declaration();
    if (fallback == nullptr) {
        return;
    }
    const RID rid = p_wrapper->get_rid_handle();
    display_book->write_param(rid, *fallback, p_param, p_value);
    const display::Decl *stored = display_book->decl_ptr(rid);
    if (stored != nullptr) {
        *fallback = *stored;
    }
}

Error NetwMultiplayer::display_declare(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_track,
    const Ref<NetwInterpolate> &p_spec
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_track.is_empty() || p_spec.is_null()) {
        return ERR_INVALID_DATA;
    }
    Node *node = entity_component_node(p_entity, p_comp);
    if (node == nullptr) {
        return ERR_UNAVAILABLE;
    }
    netw::script::model::configure_node_property(node, p_track)
        ->interpolate(gd::array_of(p_spec));
    display_book->mark_dirty(p_entity, netw::display::DIRT_RUNTIME);

    DisplayTrackRow row;
    row.comp = p_comp;
    row.spec = p_spec;
    display_declarations[p_entity.get_id()][p_track] = row;
    return OK;
}

void NetwMultiplayer::display_undeclare(const RID &p_entity) {
    const int64_t route = display_book->route_of(p_entity);
    if (route > 0) {
        display_release_route(route);
    }
    display_declarations.erase(p_entity.get_id());
    display_target_items.erase(p_entity.get_id());
    display_callbacks.erase(p_entity.get_id());
}

Error NetwMultiplayer::display_record_track(
    const RID &p_entity,
    const StringName &p_track,
    const Variant &p_value,
    int64_t p_tick
) {
    const HashMap<StringName, DisplayTrackRow> *tracks
        = display_declarations.getptr(p_entity.get_id());
    if (tracks == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    const DisplayTrackRow *row = tracks->getptr(p_track);
    if (row == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    Node *node = entity_component_node(p_entity, row->comp);
    if (node == nullptr) {
        return ERR_UNAVAILABLE;
    }
    display_record(node, p_track, p_value, p_tick, row->spec, false);
    return OK;
}

Error NetwMultiplayer::display_write_default(
    const RID &p_entity,
    const StringName &p_track,
    const Variant &p_value
) {
    const Callable *callback = display_callbacks.getptr(p_entity.get_id());
    if (callback != nullptr && callback->is_valid()) {
        callback->call(p_entity, p_track, p_value);
        return OK;
    }
    const RID *item = display_target_items.getptr(p_entity.get_id());
    if (item == nullptr || !item->is_valid()) {
        return ERR_DOES_NOT_EXIST;
    }
    switch (p_value.get_type()) {
        case Variant::TRANSFORM2D:
            RenderingServer::get_singleton()->canvas_item_set_transform(
                *item,
                p_value
            );
            return OK;
        case Variant::TRANSFORM3D:
            RenderingServer::get_singleton()->instance_set_transform(
                *item,
                p_value
            );
            return OK;
        default:
            return ERR_INVALID_DATA;
    }
}

Error NetwMultiplayer::display_write(
    const RID &p_entity,
    const StringName &p_track,
    const Variant &p_value
) {
    Error answered = OK;
    if (GDVIRTUAL_CALL(_display_write, p_entity, p_track, p_value, answered)) {
        return answered;
    }
    return display_write_default(p_entity, p_track, p_value);
}

void NetwMultiplayer::display_set_param(
    const RID &p_entity,
    DisplayParam p_param,
    const Variant &p_value
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (p_param == DISPLAY_PARAM_VISUAL_ROOT
        && p_value.get_type() != Variant::NODE_PATH
        && p_value.get_type() != Variant::STRING) {
        display_set_target_node(
            p_entity,
            entity_component_node(p_entity, int64_t(p_value))
        );
        return;
    }
    write_display_param(wrapper, p_param, p_value);
}

void NetwMultiplayer::display_set_target_node(
    const RID &p_entity,
    Node *p_node
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return;
    }
    Node *owner = wrapper->get_owner();
    if (p_node == nullptr) {
        write_display_param(wrapper, DISPLAY_PARAM_VISUAL_ROOT, NodePath());
        return;
    }
    if (owner == nullptr
        || (p_node != owner && !owner->is_ancestor_of(p_node))) {
        return;
    }
    write_display_param(
        wrapper,
        DISPLAY_PARAM_VISUAL_ROOT,
        owner->get_path_to(p_node)
    );
    display_target_items.erase(p_entity.get_id());
    display_callbacks.erase(p_entity.get_id());
}

void NetwMultiplayer::display_set_target_item(
    const RID &p_entity,
    const RID &p_item
) {
    if (entity_get_view(p_entity).is_null()) {
        return;
    }
    if (p_item.is_valid()) {
        display_target_items.insert(p_entity.get_id(), p_item);
    } else {
        display_target_items.erase(p_entity.get_id());
    }
    display_callbacks.erase(p_entity.get_id());
    display_book->mark_dirty(p_entity, netw::display::DIRT_RUNTIME);
}

void NetwMultiplayer::display_set_callback(
    const RID &p_entity,
    const Callable &p_callback
) {
    if (entity_get_view(p_entity).is_null()) {
        return;
    }
    if (p_callback.is_valid()) {
        display_callbacks.insert(p_entity.get_id(), p_callback);
    } else {
        display_callbacks.erase(p_entity.get_id());
    }
    display_target_items.erase(p_entity.get_id());
    display_book->mark_dirty(p_entity, netw::display::DIRT_RUNTIME);
}

void NetwMultiplayer::display_clear_declarations() {
    display_declarations.clear();
    display_target_items.clear();
    display_callbacks.clear();
}

Variant NetwMultiplayer::display_get_param(
    const RID &p_entity,
    DisplayParam p_param
) {
    const display::Decl *decl = display_book->decl_ptr(p_entity);
    return decl != nullptr ? decl->get_param(p_param) : Variant();
}

void NetwMultiplayer::display_reset(const RID &p_entity) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr) {
        return;
    }
    runtime->reset(
        clock_engine().get_display_offset(),
        clock_engine().recommended_display_offset()
    );
}

void NetwMultiplayer::display_snap(
    const RID &p_entity,
    const StringName &p_track,
    const Variant &p_value
) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr) {
        return;
    }
    display::Channel *channel = runtime->channel_named(p_track);
    if (channel != nullptr) {
        channel->snap(p_value);
    }
}

Variant NetwMultiplayer::display_get_value(
    const RID &p_entity,
    const StringName &p_track
) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (runtime == nullptr) {
        return Variant();
    }
    const display::Channel *channel = runtime->channel_named(p_track);
    return channel != nullptr ? channel->get_last_written() : Variant();
}

int64_t NetwMultiplayer::display_get_tick(const RID &p_entity) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    return runtime != nullptr ? runtime->authoring_tick() : -1;
}

Variant NetwMultiplayer::display_get_track_stat(
    const RID &p_entity,
    const StringName &p_track,
    const StringName &p_stat
) {
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    return runtime != nullptr ? runtime->track_stat(p_track, p_stat)
                              : Variant();
}

bool NetwMultiplayer::sync_policy_admits(
    int p_policy,
    int64_t p_sender,
    const RID &p_entity,
    int64_t p_comp
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    Node *node = entity_component_node(p_entity, p_comp);
    if (wrapper.is_null() || node == nullptr) {
        return false;
    }
    return entity::Control::policy_admits(
        p_policy,
        p_sender,
        node->get_multiplayer_authority(),
        wrapper->get_controller()
    );
}

Ref<NetwPropertySet> NetwMultiplayer::property_set_record(
    const RID &p_set
) const {
    const Ref<NetwPropertySet> *held
        = property_set_records.getptr(p_set.get_id());
    return held != nullptr ? *held : Ref<NetwPropertySet>();
}

Ref<NetwPropertySet> NetwMultiplayer::mutable_property_set(const RID &p_set) {
    const Ref<NetwPropertySet> record = property_set_record(p_set);
    NETW_ERR_COND_V(
        record.is_null(),
        Ref<NetwPropertySet>(),
        sys::TABLE,
        "a property set was addressed by a handle that names none"
    );
    NETW_ERR_COND_V(
        record->get_sealed(),
        Ref<NetwPropertySet>(),
        sys::TABLE,
        "a sealed property set cannot be changed, so the write is refused"
    );

    return record;
}

RID NetwMultiplayer::property_set_create(
    const RID &p_schema,
    RecordKind p_record
) {
    if (p_record != RECORD_KIND_STATE && p_record != RECORD_KIND_INPUT
        && p_record != RECORD_KIND_BROADCAST) {
        return RID();
    }
    const SchemaRecord *declaration = schema_core.record_of(p_schema);
    if (declaration == nullptr || schema_core.has_stride(p_schema)) {
        return RID();
    }
    const RID minted = property_sets.rid_create();
    Ref<NetwPropertySet> record = property_set_builder::for_record(p_record);
    record->set_schema(*declaration);
    record->set_rid_handle(minted);
    property_set_records.insert(minted.get_id(), record);
    property_set_schema.insert(minted.get_id(), p_schema);
    return minted;
}

int NetwMultiplayer::property_set_add_column(const RID &p_set, int p_column) {
    const Ref<NetwPropertySet> record = mutable_property_set(p_set);
    if (record.is_null()) {
        return -1;
    }
    const SchemaColumn *shape
        = SchemaCore::column_at(&record->get_schema(), p_column);
    if (shape == nullptr || record->member(p_column).is_valid()) {
        return -1;
    }
    Ref<NetwPropertySetColumn> member;
    member.instantiate();
    member->shape = *shape;
    member->set_schema_column(p_column);
    TypedArray<NetwPropertySetColumn> held = record->get_columns();
    held.push_back(member);
    record->set_columns(held);
    return int(held.size()) - 1;
}

void NetwMultiplayer::property_set_set_column_param(
    const RID &p_set,
    int p_field,
    int p_param,
    const Variant &p_value
) {
    const Ref<NetwPropertySet> record = mutable_property_set(p_set);
    if (record.is_null()) {
        return;
    }
    const TypedArray<NetwPropertySetColumn> held = record->get_columns();
    if (p_field < 0 || p_field >= int(held.size())) {
        return;
    }
    Ref<NetwPropertySetColumn> target = held[p_field];
    switch (p_param) {
        case COLUMN_PARAM_CLASS:
            target->set_property_class(int64_t(p_value));
            return;
        case COLUMN_PARAM_EPSILON:
            target->set_epsilon_override(double(p_value));
            return;
        case COLUMN_PARAM_TELEPORT_AT:
            target->set_teleport_at_override(double(p_value));
            return;
        case COLUMN_PARAM_CARRY_CHANNEL:
            target->set_carry_channel(StringName(p_value));
            return;
        case COLUMN_PARAM_TELEPORT_ONLY:
            target->set_explicit_teleport_only(bool(p_value));
            return;
        case COLUMN_PARAM_RECONCILE_ONLY:
            target->set_explicit_reconcile_only(bool(p_value));
            return;
        case COLUMN_PARAM_LANE:
            target->set_lane(int64_t(p_value));
            target->set_watch(target->get_lane() == NetwPropertySet::RETAINED);
            record->reproject_lanes();
            return;
        case COLUMN_PARAM_CONVERGE_STIFFNESS:
            target->set_converge_stiffness(double(p_value));
            return;
        default:
            NETW_ERR(
                sys::TABLE,
                "column parameter {} names no column setting",
                p_param
            );
    }
}

void NetwMultiplayer::property_set_set_param(
    const RID &p_set,
    int p_param,
    const Variant &p_value
) {
    const Ref<NetwPropertySet> record = mutable_property_set(p_set);
    if (record.is_null()) {
        return;
    }
    switch (p_param) {
        case SET_PARAM_MASKED:
            record->set_masked(bool(p_value));
            return;
        case SET_PARAM_WINDOW:
            record->set_window(int64_t(p_value));
            return;
        case SET_PARAM_AUDIENCE:
            record->set_audience(int64_t(p_value));
            return;
        case SET_PARAM_POLICY:
            record->set_policy(
                static_cast<NetwMemberConfig::Policy>(int(p_value))
            );
            return;
        case SET_PARAM_TRIGGER:
            record->set_trigger(int64_t(p_value));
            return;
        case SET_PARAM_CADENCE:
            record->set_cadence(int64_t(p_value));
            return;
        case SET_PARAM_STAMP:
            record->set_stamp(int64_t(p_value));
            return;
        case SET_PARAM_PROFILE:
            record->set_profile(int64_t(p_value));
            return;
        case SET_PARAM_CHANNEL:
            record->set_channel(int64_t(p_value));
            return;
        case SET_PARAM_RELIABLE:
            record->set_reliable(bool(p_value));
            return;
        default:
            NETW_ERR(
                sys::TABLE,
                "set parameter {} names no property set setting",
                p_param
            );
    }
}

void NetwMultiplayer::property_set_clear() {
    property_set_by_script.clear();
    entity_property_sets.clear();
    property_set_records.clear();
    property_set_schema.clear();
    property_sets.clear();
}

RID NetwMultiplayer::script_schema(const Ref<Script> &p_script, Node *p_node) {
    StringName name("@anonymous_script");
    if (p_script.is_valid()) {
        name = StringName(
            p_script->get_path().is_empty() ? "@script:"
                    + String::num_int64(int64_t(p_script->get_instance_id()))
                                            : p_script->get_path()
        );
    }
    const RID schema = schema_create(name);
    const Dictionary configs
        = netw::script::model::get_property_configs(p_script);
    const Array properties = configs.keys();
    for (int at = 0; at < properties.size(); ++at) {
        const StringName property = properties[at];
        const Ref<NetwPropertyConfig> config = configs[property];
        if (config.is_null()) {
            continue;
        }
        const int column = schema_add_column(
            schema,
            property,
            static_cast<ColumnType>(
                NetwPropertySet::column_type_for(p_script, p_node, property)
            ),
            1
        );
        const Array quantizers = config->get_quantizers();
        if (column >= 0 && !quantizers.is_empty()) {
            schema_set_column_quantizer(schema, column, quantizers[0]);
        }
    }
    if (schema_seal(schema) != OK) {
        NETW_ERROR(
            sys::TABLE,
            "script '%s' recompiled its properties with a different shape, so "
            "peers built from the two cannot read each other",
            String(name)
        );
    }
    return schema;
}

RID NetwMultiplayer::adopt_property_set(
    const Ref<Script> &p_script,
    RecordKind p_record_kind,
    const Ref<NetwPropertySet> &p_source,
    Node *p_node
) {
    if (p_source.is_null()) {
        return RID();
    }
    const uint64_t key = p_script.is_valid()
        ? uint64_t(p_script->get_instance_id())
        : uint64_t(0);
    HashMap<int, RID> *cached = property_set_by_script.getptr(key);
    if (cached != nullptr) {
        const RID *found = cached->getptr(p_record_kind);
        if (found != nullptr) {
            return *found;
        }
    }
    const RID schema = script_schema(p_script, p_node);
    const RID set = property_set_create(schema, p_record_kind);
    if (!set.is_valid()) {
        return set;
    }
    property_set_set_param(set, SET_PARAM_MASKED, p_source->get_masked());
    property_set_set_param(set, SET_PARAM_WINDOW, p_source->get_window());
    property_set_set_param(set, SET_PARAM_AUDIENCE, p_source->get_audience());
    property_set_set_param(set, SET_PARAM_POLICY, p_source->get_policy());
    property_set_set_param(set, SET_PARAM_TRIGGER, p_source->get_trigger());
    property_set_set_param(set, SET_PARAM_CADENCE, p_source->get_cadence());
    property_set_set_param(set, SET_PARAM_STAMP, p_source->get_stamp());
    property_set_set_param(set, SET_PARAM_PROFILE, p_source->get_profile());
    property_set_set_param(set, SET_PARAM_CHANNEL, p_source->get_channel());
    property_set_set_param(set, SET_PARAM_RELIABLE, p_source->get_reliable());
    const TypedArray<NetwPropertySetColumn> columns = p_source->get_columns();
    for (int index = 0; index < columns.size(); ++index) {
        const Ref<NetwPropertySetColumn> member = columns[index];
        if (member.is_null()) {
            continue;
        }
        const int column = schema_core.find_column(schema, member->get_key());
        const int at = property_set_add_column(set, column);
        if (at < 0) {
            continue;
        }
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_CLASS,
            member->get_property_class()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_EPSILON,
            member->get_epsilon_override()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_TELEPORT_AT,
            member->get_teleport_at_override()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_CARRY_CHANNEL,
            member->get_carry_channel()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_TELEPORT_ONLY,
            member->get_explicit_teleport_only()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_RECONCILE_ONLY,
            member->get_explicit_reconcile_only()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_LANE,
            member->get_lane()
        );
        property_set_set_column_param(
            set,
            at,
            COLUMN_PARAM_CONVERGE_STIFFNESS,
            member->get_converge_stiffness()
        );
    }
    property_set_seal(set);
    property_set_by_script[key][p_record_kind] = set;
    return set;
}

Error NetwMultiplayer::property_set_seal(const RID &p_set) {
    const Ref<NetwPropertySet> record = property_set_record(p_set);
    if (record.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    record->seal();
    return OK;
}

int64_t NetwMultiplayer::property_set_get_wire_hash(const RID &p_set) const {
    const Ref<NetwPropertySet> record = property_set_record(p_set);
    return record.is_valid() ? record->wire_hash() : 0;
}

RID NetwMultiplayer::property_set_get_schema(const RID &p_set) const {
    const RID *held = property_set_schema.getptr(p_set.get_id());
    return held != nullptr ? *held : RID();
}

Ref<NetwPromise> NetwMultiplayer::persist_hydrate(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return NetwPromise::resolved(ERR_DOES_NOT_EXIST);
    }
    const Ref<NetwPersistenceEngine> engine
        = persistence_engine_for(wrapper.ptr());
    if (engine.is_null()) {
        return NetwPromise::resolved(ERR_UNCONFIGURED);
    }
    return engine->hydrate();
}

Ref<NetwPromise> NetwMultiplayer::persist_flush(
    const RID &p_entity,
    const Array &p_keys
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return NetwPromise::resolved(ERR_DOES_NOT_EXIST);
    }
    const Ref<NetwPersistenceEngine> engine
        = persistence_engine_for(wrapper.ptr());
    if (engine.is_null()) {
        return NetwPromise::resolved(ERR_UNCONFIGURED);
    }
    return engine->flush(p_keys);
}

void NetwMultiplayer::lagcomp_effect_adopt(const StringName &p_key) {
    effects.adopt(p_key);
}

void NetwMultiplayer::lagcomp_effect_discard(const StringName &p_key) {
    effects.discard(p_key);
}

bool NetwMultiplayer::lagcomp_effect_pending(const StringName &p_key) const {
    return effects.pending(p_key);
}

int64_t NetwMultiplayer::lagcomp_effect_count() const {
    return effects.count();
}

int64_t NetwMultiplayer::event_watch(
    const PackedInt64Array &p_events,
    const Dictionary &p_target,
    const Dictionary &p_predicate,
    const Callable &p_sink,
    const Dictionary &p_opts
) {
    return plane.watch(p_events, p_target, p_predicate, p_sink, p_opts);
}

bool NetwMultiplayer::event_unwatch(int64_t p_id) {
    return plane.unwatch(p_id);
}

TypedArray<Dictionary> NetwMultiplayer::event_watches() const {
    return plane.watches();
}

TypedArray<Dictionary> NetwMultiplayer::event_ring(int64_t p_route) {
    return plane.ring(p_route);
}

void NetwMultiplayer::event_ring_clear(int64_t p_route) {
    plane.ring_clear(p_route);
}

void NetwMultiplayer::event_arm(bool p_enabled) {
    plane.set_armed(p_enabled);
}

bool NetwMultiplayer::event_wants(int64_t p_event, int64_t p_route) const {
    return plane.wants(p_event, p_route);
}

void NetwMultiplayer::event_emit(
    int64_t p_event,
    int64_t p_route,
    const Dictionary &p_detail,
    const StringName &p_entity_id,
    int64_t p_peer,
    int64_t p_verdict,
    const Dictionary &p_model
) {
    if (!plane.wants(p_event, p_route)) {
        return;
    }
    EventPlane::Emission fact(
        p_event,
        EventPlane::AFTER,
        clock_engine().get_tick()
    );
    fact.route = p_route;
    fact.detail = p_detail;
    fact.entity_id = p_entity_id;
    fact.peer = p_peer;
    fact.verdict = p_verdict;
    fact.model = p_model;
    plane.emit(fact);
}

void NetwMultiplayer::lagcomp_effect_sweep(int64_t p_tick) {
    effects.sweep(p_tick);
}

PackedByteArray NetwMultiplayer::sync_encode(int64_t p_peer, int64_t p_tick) {
    PackedByteArray answered;
    if (GDVIRTUAL_CALL(_sync_encode, p_peer, p_tick, answered)) {
        return answered;
    }
    return sync_encode_armed ? sync_encode_stock : PackedByteArray();
}

Error NetwMultiplayer::sync_decode(
    const RID &p_entity,
    int64_t p_comp,
    int64_t p_flags,
    int64_t p_tick,
    const PackedByteArray &p_payload
) {
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _sync_decode,
            p_entity,
            p_comp,
            p_flags,
            p_tick,
            p_payload,
            answered
        )) {
        return answered;
    }
    if (!sync_decoder.is_valid()) {
        return ERR_UNCONFIGURED;
    }
    return Error(int(sync_decoder.call()));
}

PackedByteArray NetwMultiplayer::run_sync_encode_stage(
    int64_t p_peer,
    const PackedByteArray &p_stock,
    int64_t p_tick
) {
    NETW_ZONE_NC("sync encode stage", colors::CODEC);
    sync_encode_stock = p_stock;
    sync_encode_armed = true;
    int64_t stamped = p_tick;
    if (stamped < 0) {
        stamped = clock_engine().get_configured()
            ? int64_t(clock_engine().get_tick())
            : 0;
    }
    const PackedByteArray bytes = sync_encode(p_peer, stamped);
    sync_encode_armed = false;
    sync_encode_stock = PackedByteArray();
    NETW_TRACE(
        sys::CODEC,
        "encode stage answered %d bytes for peer %d at tick %d",
        int(bytes.size()),
        int(p_peer),
        int(stamped)
    );
    return bytes;
}

Error NetwMultiplayer::run_sync_decode_stage(
    const RID &p_entity,
    int64_t p_comp,
    int64_t p_flags,
    int64_t p_tick,
    const PackedByteArray &p_payload,
    const Callable &p_decoder
) {
    NETW_ZONE_NC("sync decode stage", colors::CODEC);
    sync_decoder = p_decoder;
    const Error verdict
        = sync_decode(p_entity, p_comp, p_flags, p_tick, p_payload);
    sync_decoder = Callable();
    NETW_TRACE(
        sys::CODEC,
        "decode stage answered %d for comp %d at tick %d",
        int(verdict),
        int(p_comp),
        int(p_tick)
    );
    return verdict;
}

} // namespace netw
