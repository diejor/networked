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
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;
using namespace netw;

namespace netw {

interest::Engine &NetwMultiplayer::interest_plane() {
    return interest_engine;
}

void NetwMultiplayer::reset_interest() {
    interest_engine.clear();
}

void NetwMultiplayer::interest_sync_record(NetwEntity *p_wrapper) {
    NetwEntity *current = p_wrapper;
    LocalVector<int64_t> keys;
    LocalVector<int64_t> routes;
    HashSet<int64_t> seen;
    while (current != nullptr) {
        const RID handle = liveness_adopt(current);
        const int64_t key = handle.get_id();
        if (!interest::Engine::is_key(key) || seen.has(key)) {
            break;
        }
        seen.insert(key);
        keys.push_back(key);
        routes.push_back(current->get_route());
        Node *owner = wrapper_owner(handle);
        current = owner == nullptr ? nullptr
                                   : Object::cast_to<NetwEntity>(
                                         wrapper_at(owner->get_parent()).ptr()
                                     );
    }
    for (uint32_t index = keys.size(); index-- > 0;) {
        const int64_t key = keys[index];
        const int64_t parent_key
            = index + 1 < keys.size() ? keys[index + 1] : 0;
        interest_engine.set_parent(key, parent_key);
        int64_t route = routes[index];
        if (route <= 0) {
            route = interest_engine.order_route_for(key);
        }
        interest_engine
            .set_order_key(key, int(keys.size() - 1 - index), int(route));
    }
}

void NetwMultiplayer::interest_track_lifecycle(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    liveness_adopt(p_entity.ptr());
    interest_engine.set_exit_handler(
        p_entity->get_rid_handle().get_id(),
        Callable()
    );
}

void NetwMultiplayer::interest_untrack_lifecycle(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    const Callable handler = interest_engine.take_exit_handler(
        p_entity->get_rid_handle().get_id()
    );
    Node *owner = p_entity->get_owner();
    if (handler.is_valid() && owner != nullptr
        && owner->is_connected("tree_exiting", handler)) {
        owner->disconnect("tree_exiting", handler);
    }
}

Dictionary NetwMultiplayer::interest_leave_resolve(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    if (p_entity.is_null()) {
        return Dictionary();
    }
    const RID handle = p_entity->get_rid_handle();
    return interest_leave_engine.resolve(
        handle.get_id(),
        p_peer_id,
        interest_decl_on(p_entity),
        interest_engine
    );
}

void NetwMultiplayer::interest_leave_commit(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id,
    const Dictionary &p_decision,
    bool p_forced
) {
    if (p_entity.is_null()) {
        return;
    }
    interest_leave_engine.commit(
        p_entity->get_rid_handle().get_id(),
        p_peer_id,
        p_decision,
        p_forced
    );
}

void NetwMultiplayer::interest_leave_finish_sweep() {
    interest_leave_engine.finish_sweep();
}

void NetwMultiplayer::interest_retire_entity(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (slot == 0 || !interest_engine.has_entity(slot)) {
        return;
    }
    interest_engine.remove_entity(slot);
}

void NetwMultiplayer::interest_release_body(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const Array named = interest_engine.memberships(slot);
    for (int at = 0; at < named.size(); ++at) {
        const Ref<NetwInterestLayer> exiting = interest_layer_named(named[at]);
        if (exiting.is_null()) {
            continue;
        }
        if (is_server()) {
            exiting->remove_entity(p_entity);
        } else {
            exiting->client_untrack_entity(p_entity);
        }
    }
    interest_untrack_lifecycle(p_entity);
    interest_retire_entity(p_entity);
    interest_leave_engine.forget_entity(slot);
    interest_engine.set_scene_membership(slot, StringName());
    interest_clear_perception(p_entity, false);
}

void NetwMultiplayer::interest_refresh_perception(
    const Ref<NetwEntity> &p_entity,
    const Array &p_layer_ids
) {
    const int64_t peer_id = interest_local_participant();
    if (peer_id == 0 || p_entity.is_null()
        || p_entity->get_owner() == nullptr) {
        return;
    }
    const RID handle = p_entity->get_rid_handle();
    const int64_t slot = handle.get_id();
    if (is_server()) {
        if (!interest_engine.has_entity(slot)) {
            return;
        }
    } else if (interest_membership_ids(handle).is_empty()) {
        return;
    }
    const bool visible = interest_participant_sees(peer_id, p_entity);
    if (!interest_perception.set_visible(slot, visible)) {
        return;
    }
    if (visible) {
        perception_restore(slot);
        perception_dispatch_custom(slot, true, peer_id);
        return;
    }
    Array layers = p_layer_ids;
    if (layers.is_empty()) {
        layers = perception_layers_for(p_entity, peer_id);
    }
    const Dictionary verdict = interest_perception.resolve(
        layers,
        interest_decl_on(p_entity),
        interest_engine
    );
    if (bool(verdict[StringName("hide")])) {
        perception_hide(slot, p_entity->get_owner());
    }
    const Array actions = verdict[StringName("custom")];
    interest_perception.arm(slot, actions);
    for (int at = 0; at < actions.size(); ++at) {
        const Array action = actions[at];
        const Callable callback = action[0];
        if (callback.is_valid()) {
            callback.call(false, peer_id, action[1]);
        }
    }
}

void NetwMultiplayer::interest_refresh_all_perception() {
    NETW_ZONE_NC(
        "NetwMultiplayer interest refresh all perception",
        colors::INTEREST
    );
    if (interest_local_participant() == 0) {
        return;
    }
    const PackedInt64Array keys = interest_engine.membership_keys();
    for (int at = 0; at < int(keys.size()); ++at) {
        const Ref<NetwEntity> member = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(keys[at]).ptr())
        );
        if (member.is_valid()) {
            interest_refresh_perception(member, Array());
        }
    }
}

void NetwMultiplayer::interest_reapply_perception(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    if (interest_perception.is_known(p_entity->get_rid_handle().get_id())) {
        interest_clear_perception(p_entity, true);
    }
    interest_refresh_perception(p_entity, Array());
}

void NetwMultiplayer::interest_clear_perception(
    const Ref<NetwEntity> &p_entity,
    bool p_restore
) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (p_restore) {
        perception_restore(slot);
        perception_dispatch_custom(slot, true, interest_local_participant());
    } else {
        perception_snapshots.erase(slot);
        interest_perception.disarm(slot);
    }
    interest_perception.forget(slot);
}

void NetwMultiplayer::interest_clear_all_perception() {
    LocalVector<int64_t> hidden;
    for (const KeyValue<int64_t, LocalVector<PerceptionSnapshot>> &row :
         perception_snapshots) {
        hidden.push_back(row.key);
    }
    for (uint32_t at = 0; at < hidden.size(); ++at) {
        perception_restore(hidden[at]);
    }
    const PackedInt64Array armed = interest_perception.armed_keys();
    const int64_t peer_id = interest_local_participant();
    for (int at = 0; at < int(armed.size()); ++at) {
        perception_dispatch_custom(armed[at], true, peer_id);
    }
    interest_perception.clear();
    perception_snapshots.clear();
}

namespace {

constexpr int64_t CLOCKLESS_AWARENESS_TICKRATE = 30;

} // namespace

void NetwMultiplayer::interest_receive_awareness(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    LocalVector<interest::Awareness> events;
    if (!interest::awareness_decode(p_payload, events)) {
        NETW_TRACE(
            sys::INTEREST,
            "an awareness batch that did not decode whole is refused"
        );
        return;
    }
    for (uint32_t at = 0; at < events.size(); ++at) {
        const interest::Awareness &edge = events[at];
        const bool entered = edge.kind == interest::Awareness::ENTER;
        if (!entered
            && liveness_route_state(edge.route)
                != NetwLivenessCore::STATE_LIVE) {
            continue;
        }
        const bool clocked = clock_engine().get_configured();
        const int64_t origin
            = clocked ? clock_engine().get_tick() : liveness_core->frame();
        const int64_t timeout = clocked ? clock_engine().get_tickrate()
                                        : CLOCKLESS_AWARENESS_TICKRATE;
        liveness_schedule_when_live(
            edge.route,
            callable_mp(this, &NetwMultiplayer::interest_apply_awareness)
                .bind(
                    edge.type,
                    edge.route,
                    edge.layer_id,
                    edge.observer_peer,
                    edge.kind
                ),
            origin + timeout,
            clocked,
            Callable()
        );
    }
}

void NetwMultiplayer::interest_apply_awareness(
    int p_edge_type,
    int64_t p_route,
    const StringName &p_layer_id,
    int64_t p_observer_peer,
    int p_kind
) {
    const Ref<NetwEntity> entity = Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(wrapper_for_route(p_route).ptr())
    );
    if (entity.is_null()) {
        return;
    }
    interest::Awareness edge;
    edge.type = int32_t(p_edge_type);
    edge.route = p_route;
    edge.layer_id = p_layer_id;
    edge.observer_peer = p_observer_peer;
    edge.kind = int32_t(p_kind);
    const bool entered = edge.kind == interest::Awareness::ENTER;
    if (edge.type == interest::Awareness::LAYER) {
        const Ref<NetwInterestLayer> event_layer = interest_layer(p_layer_id);
        if (event_layer.is_null()) {
            return;
        }
        interest_report_edge(edge, entity, entered, true);
        if (entered) {
            event_layer->client_admit(entity);
        } else {
            event_layer->client_revoke(entity);
        }
        return;
    }
    interest_report_edge(edge, entity, entered, false);
    entity->emit_signal(
        entered ? "observer_entered" : "observer_left",
        p_layer_id,
        p_observer_peer
    );
}

void NetwMultiplayer::interest_report_edge(
    const interest::Awareness &p_edge,
    const Ref<NetwEntity> &p_entity,
    bool p_entered,
    bool p_layer_edge
) {
    int64_t value
        = p_entered ? EventPlane::INTEREST_ENTER : EventPlane::INTEREST_EXIT;
    if (!p_layer_edge) {
        value = p_entered ? EventPlane::OBSERVER_ENTERED
                          : EventPlane::OBSERVER_LEFT;
    }
    if (!event_wants(value, p_edge.route)) {
        return;
    }
    Dictionary detail;
    detail[StringName("layer")] = p_edge.layer_id;
    event_emit(
        value,
        p_edge.route,
        detail,
        p_entity->get_entity_id(),
        p_edge.observer_peer,
        OK,
        Dictionary()
    );
}

bool NetwMultiplayer::interest_can_send_to(int64_t p_peer_id) {
    if (p_peer_id == 0 || p_peer_id == MultiplayerPeer::TARGET_PEER_SERVER) {
        return false;
    }
    if (!has_multiplayer_peer()) {
        return false;
    }
    const Ref<MultiplayerPeer> live = get_multiplayer_peer();
    if (live.is_null() || live->is_class("OfflineMultiplayerPeer")) {
        return false;
    }
    if (live->get_connection_status()
        != MultiplayerPeer::CONNECTION_CONNECTED) {
        return false;
    }
    if (is_server()) {
        return NETW_API_VIRTUAL(get_peer_ids)().has(int32_t(p_peer_id));
    }
    return false;
}

void NetwMultiplayer::interest_queue_layer_awareness(
    const StringName &p_layer_id,
    const Ref<NetwEntity> &p_entity,
    int64_t p_observer_peer,
    int p_kind
) {
    if (!is_server() || p_entity.is_null()) {
        return;
    }
    Node *owner = p_entity->get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    if (!interest_can_send_to(p_observer_peer)) {
        return;
    }
    interest_awareness_queue_layer(
        p_observer_peer,
        liveness_allocate_route(p_entity.ptr()),
        p_layer_id,
        p_kind
    );
    interest_request_flush();
}

void NetwMultiplayer::interest_queue_observer_awareness(
    const StringName &p_layer_id,
    const Ref<NetwEntity> &p_entity,
    int64_t p_observer_peer,
    int p_kind
) {
    NETW_ZONE_NC(
        "NetwMultiplayer observer awareness echo gate",
        colors::INTEREST
    );
    if (!is_server() || p_entity.is_null()) {
        return;
    }
    const int64_t owner_peer = p_entity->get_peer_id();
    const bool self_echo = owner_peer != 0 && p_observer_peer == owner_peer;
    NETW_ZONE_VALUE(int64_t(self_echo));
    if (owner_peer == 0 || self_echo) {
        return;
    }
    interest::Decl *decl = interest_decl_on(p_entity);
    if (decl == nullptr || !decl->get_reports_observers()) {
        return;
    }
    Node *owner = p_entity->get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    if (!interest_can_send_to(owner_peer)) {
        return;
    }
    interest_awareness_queue_observer(
        owner_peer,
        liveness_allocate_route(p_entity.ptr()),
        p_layer_id,
        p_observer_peer,
        p_kind
    );
    interest_request_flush();
}

void NetwMultiplayer::interest_layer_transition(
    const Ref<NetwInterestLayer> &p_layer,
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id,
    bool p_visible
) {
    p_layer->apply_server_transition(p_entity, p_peer_id, p_visible);
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const StringName layer_id = p_layer->get_layer_id();
    if (p_visible) {
        interest_leave_engine.release(slot, p_peer_id);
    } else {
        interest_leave_engine.record(slot, p_peer_id, layer_id);
    }
    const int kind = p_visible ? 1 : 0;
    interest_queue_layer_awareness(layer_id, p_entity, p_peer_id, kind);
    interest_queue_observer_awareness(layer_id, p_entity, p_peer_id, kind);
}

void NetwMultiplayer::interest_apply_delta(const interest::Delta &p_delta) {
    NETW_ZONE_NC("NetwMultiplayer interest apply delta", colors::INTEREST);
    for (int pass = 0; pass < 2; ++pass) {
        const bool visible = pass == 1;
        const Array rows
            = visible ? p_delta.get_layer_shows() : p_delta.get_layer_hides();
        for (int at = 0; at < rows.size(); ++at) {
            const Array row = rows[at];
            const StringName layer_id = row[0];
            const Ref<NetwEntity> entity
                = Ref<NetwEntity>(Object::cast_to<NetwEntity>(
                    wrapper_for_id(int64_t(row[1])).ptr()
                ));
            const int64_t peer_id = interest_engine.peer_of_bit(int(row[2]));
            const Ref<NetwInterestLayer> layer = interest_layer_named(layer_id);
            if (layer.is_null() || entity.is_null() || peer_id == 0) {
                continue;
            }
            interest_layer_transition(layer, entity, peer_id, visible);
        }
    }
    const Array shows = p_delta.get_shows();
    for (int at = 0; at < shows.size(); ++at) {
        const Array row = shows[at];
        const int64_t slot = int64_t(row[0]);
        if (wrapper_for_id(slot).is_valid()) {
            interest_leave_engine.release(
                slot,
                interest_engine.peer_of_bit(int(row[1]))
            );
        }
    }
}

int64_t NetwMultiplayer::interest_local_participant() {
    if (!is_local_client()) {
        return 0;
    }
    if (session_get_role() == ROLE_LISTEN_SERVER) {
        return MultiplayerPeer::TARGET_PEER_SERVER;
    }
    if (has_multiplayer_peer()) {
        return get_unique_id();
    }
    return 0;
}

Array NetwMultiplayer::perception_layers_for(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    const RID handle = p_entity->get_rid_handle();
    const Array pending
        = interest_leave_engine.pending_layers(handle.get_id(), p_peer_id);
    if (!pending.is_empty()) {
        return pending;
    }
    return interest_membership_ids(handle);
}

void NetwMultiplayer::perception_hide(int64_t p_slot, Node *p_owner) {
    if (perception_snapshots.has(p_slot) || p_owner == nullptr) {
        return;
    }
    LocalVector<PerceptionSnapshot> snapshot;
    LocalVector<Node *> stack;
    stack.push_back(p_owner);
    while (!stack.is_empty()) {
        Node *node = stack[stack.size() - 1];
        stack.remove_at(stack.size() - 1);
        if (node->is_class("CanvasItem") || node->is_class("Node3D")) {
            const StringName prop = StringName("visible");
            PerceptionSnapshot row;
            row.node = node->get_instance_id();
            row.prop = prop;
            row.value = node->get(prop);
            snapshot.push_back(row);
            node->set(prop, false);
        }
        if (node->is_class("AudioStreamPlayer")
            || node->is_class("AudioStreamPlayer2D")
            || node->is_class("AudioStreamPlayer3D")) {
            const StringName prop = StringName("volume_db");
            PerceptionSnapshot row;
            row.node = node->get_instance_id();
            row.prop = prop;
            row.value = node->get(prop);
            snapshot.push_back(row);
            node->set(prop, -80.0);
        }
        const TypedArray<Node> children = node->get_children();
        for (int at = 0; at < children.size(); ++at) {
            Node *child = Object::cast_to<Node>(children[at]);
            if (child != nullptr && !child->has_meta(wrapper_meta())) {
                stack.push_back(child);
            }
        }
    }
    perception_snapshots[p_slot] = snapshot;
}

void NetwMultiplayer::perception_restore(int64_t p_slot) {
    const LocalVector<PerceptionSnapshot> *held
        = perception_snapshots.getptr(p_slot);
    if (held == nullptr) {
        return;
    }
    LocalVector<PerceptionSnapshot> snapshot;
    for (uint32_t at = 0; at < held->size(); ++at) {
        snapshot.push_back((*held)[at]);
    }
    for (uint32_t at = 0; at < snapshot.size(); ++at) {
        Object *node = ObjectDB::get_instance(snapshot[at].node);
        if (node != nullptr) {
            node->set(snapshot[at].prop, snapshot[at].value);
        }
    }
    perception_snapshots.erase(p_slot);
}

void NetwMultiplayer::perception_dispatch_custom(
    int64_t p_slot,
    bool p_visible,
    int64_t p_peer_id
) {
    const Array actions = interest_perception.disarm(p_slot);
    for (int at = 0; at < actions.size(); ++at) {
        const Array action = actions[at];
        const Callable callback = action[0];
        if (callback.is_valid()) {
            callback.call(p_visible, p_peer_id, action[1]);
        }
    }
}

interest::Decl *NetwMultiplayer::interest_decl_on(
    const Ref<NetwEntity> &p_entity
) {
    interest::Facet *facet = interest_facet_on(p_entity);
    return facet != nullptr ? facet->declaration() : nullptr;
}

bool NetwMultiplayer::interest_participant_sees(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    if (p_peer_id == 0 || p_entity.is_null()) {
        return false;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (!is_server()) {
        return interest_engine.projection_admits(
            slot,
            interest_decl_on(p_entity)
        );
    }
    const int bit = interest_engine.peer_bit_of(p_peer_id);
    if (bit < 0) {
        return false;
    }
    return interest_engine.test(slot, bit);
}

namespace {

Array sorted_names(const Array &p_names) {
    LocalVector<StringName> ordered;
    for (int at = 0; at < p_names.size(); ++at) {
        const StringName id = p_names[at];
        uint32_t seat = ordered.size();
        while (seat > 0 && String(id) < String(ordered[seat - 1])) {
            --seat;
        }
        ordered.insert(seat, id);
    }
    Array out;
    for (uint32_t at = 0; at < ordered.size(); ++at) {
        out.push_back(ordered[at]);
    }
    return out;
}

} // namespace

Array NetwMultiplayer::interest_resolved_layer_ids(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Array();
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    Array named = interest_engine.memberships(slot);
    if (named.is_empty() && !is_server()) {
        interest::Decl *decl = interest_decl_on(p_entity);
        named = decl != nullptr ? decl->labels() : Array();
    }
    return sorted_names(named);
}

TypedArray<Object> NetwMultiplayer::interest_shared_entities(
    const Ref<NetwEntity> &p_entity,
    const StringName &p_layer_id
) {
    TypedArray<Object> out;
    if (p_entity.is_null()) {
        return out;
    }
    const Array resolved = interest_resolved_layer_ids(p_entity);
    Array wanted;
    if (p_layer_id.is_empty()) {
        wanted = resolved;
    } else {
        if (!resolved.has(p_layer_id)) {
            return out;
        }
        wanted.push_back(p_layer_id);
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    LocalVector<Ref<NetwEntity>> found;
    const PackedInt64Array co = interest_engine.co_members(slot, wanted);
    for (int at = 0; at < int(co.size()); ++at) {
        const Ref<NetwEntity> candidate = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(co[at]).ptr())
        );
        if (candidate.is_valid() && candidate->get_owner() != nullptr) {
            found.push_back(candidate);
        }
    }
    if (found.is_empty() && !is_server()) {
        HashSet<StringName> asked;
        for (int at = 0; at < wanted.size(); ++at) {
            asked.insert(wanted[at]);
        }
        const TypedArray<Object> live = liveness_get_entities();
        for (int at = 0; at < live.size(); ++at) {
            const Ref<NetwEntity> candidate
                = Ref<NetwEntity>(Object::cast_to<NetwEntity>(live[at]));
            if (candidate.is_null() || candidate == p_entity
                || candidate->get_owner() == nullptr) {
                continue;
            }
            interest::Decl *decl = interest_decl_on(candidate);
            const Array labels = decl != nullptr ? decl->labels() : Array();
            for (int seat = 0; seat < labels.size(); ++seat) {
                if (asked.has(labels[seat])) {
                    found.push_back(candidate);
                    break;
                }
            }
        }
    }
    LocalVector<Ref<NetwEntity>> ordered;
    for (uint32_t at = 0; at < found.size(); ++at) {
        const Ref<NetwEntity> row = found[at];
        const String key = String(row->get_entity_id());
        uint32_t seat = ordered.size();
        while (seat > 0) {
            const Ref<NetwEntity> before = ordered[seat - 1];
            const String other = String(before->get_entity_id());
            const bool after = other < key
                || (other == key
                    && before->get_instance_id() < row->get_instance_id());
            if (after) {
                break;
            }
            --seat;
        }
        ordered.insert(seat, row);
    }
    for (uint32_t at = 0; at < ordered.size(); ++at) {
        out.push_back(ordered[at]);
    }
    return out;
}

Dictionary NetwMultiplayer::interest_monitor_snapshot() {
    Dictionary out;
    out[StringName("layers")] = interest_layers().size();
    out[StringName("entities_filtered")]
        = interest_engine.membership_keys().size();
    out[StringName("visible_edges")] = interest_engine.stats_edges();
    out[StringName("dirty_entities")] = interest_engine.dirty_count();
    out[StringName("relay_backlog")] = liveness_pending_live_count();
    out[StringName("transitions_total")] = interest_engine.transitions_total();
    out[StringName("vanished_dirty_skips")]
        = interest_engine.stats_vanished_dirty_skips();
    return out;
}

bool NetwMultiplayer::interest_wire_admits(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    if (p_peer_id == MultiplayerPeer::TARGET_PEER_SERVER) {
        return true;
    }
    return interest_participant_sees(p_peer_id, p_entity);
}

bool NetwMultiplayer::interest_has_committed_intent(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return false;
    }
    return interest_engine.had_committed_intent(
        p_entity->get_rid_handle().get_id()
    );
}

PackedInt64Array NetwMultiplayer::interest_committed_row(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return PackedInt64Array();
    }
    return interest_engine.row_of(p_entity->get_rid_handle().get_id());
}

bool NetwMultiplayer::interest_bit_admits(
    const Ref<NetwEntity> &p_entity,
    int p_peer_bit
) {
    if (p_entity.is_null()) {
        return false;
    }
    return interest_engine.test(
        p_entity->get_rid_handle().get_id(),
        p_peer_bit
    );
}

String NetwMultiplayer::interest_explain_bit(
    const Ref<NetwEntity> &p_entity,
    int p_peer_bit
) {
    if (p_entity.is_null()) {
        return String("entity is not registered");
    }
    return interest_engine.explain(
        p_entity->get_rid_handle().get_id(),
        p_peer_bit
    );
}

void NetwMultiplayer::interest_forget_layer_row(const StringName &p_layer_id) {
    interest_engine.remove_layer(p_layer_id);
}

int NetwMultiplayer::interest_peer_bit(int64_t p_peer_id) const {
    return interest_engine.peer_bit_of(p_peer_id);
}

bool NetwMultiplayer::interest_has_layer(const StringName &p_layer_id) const {
    return interest_engine.has_layer(p_layer_id);
}

bool NetwMultiplayer::interest_entity_has_filter(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return false;
    }
    if (!is_server()) {
        interest::Decl *decl = interest_decl_on(p_entity);
        return decl != nullptr && !decl->labels().is_empty();
    }
    return interest_engine.has_memberships(p_entity->get_rid_handle().get_id());
}

void NetwMultiplayer::interest_peer_connected(int64_t p_peer_id) {
    interest_engine.peer_bit_for(p_peer_id);
    interest_sync_live_peers();
    if (interest_compat_refresh.is_valid()) {
        session_defer(
            interest_compat_refresh,
            StringName("interest_compat_intents")
        );
    }
    interest_request_flush();
}

void NetwMultiplayer::interest_peer_disconnected(int64_t p_peer_id) {
    interest_awareness_forget(p_peer_id);
    interest_leave_engine.forget_peer(p_peer_id);
    interest_engine.forget_peer(p_peer_id);
    interest_sync_live_peers();
    interest_request_flush();
}

void NetwMultiplayer::interest_session_ended() {
    callable_mp(this, &NetwMultiplayer::interest_clear_session).call_deferred();
}

void NetwMultiplayer::interest_clear_session() {
    layer_forget_all();
    interest_leave_engine.clear();
    interest_clear_all_perception();
    interest_awareness_clear();
    reset_interest();
    session_cancel_deferred(interest_flush_key());
    interest_pending = interest::Delta();
    interest_pending_live = false;
}

void NetwMultiplayer::interest_sync_scene_membership(
    const Ref<NetwEntity> &p_entity
) {
    if (!is_server() || p_entity.is_null()
        || p_entity->get_owner() == nullptr) {
        return;
    }
    liveness_adopt(p_entity.ptr());
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const StringName previous = interest_engine.scene_membership(slot);
    const RID scene = scene_of(entity_of(p_entity->get_owner()));
    const StringName current
        = scene.is_valid() ? scene_layer_id(scene) : StringName();
    if (!interest_engine.set_scene_membership(slot, current)) {
        return;
    }
    if (!previous.is_empty()) {
        const Ref<NetwInterestLayer> before = interest_layer_named(previous);
        if (before.is_valid()) {
            before->remove_entity(p_entity);
        }
    }
    if (!current.is_empty()) {
        interest_layer(current)->add_entity(p_entity);
    }
}

PackedInt64Array NetwMultiplayer::interest_known_peers() const {
    return interest_engine.known_peers();
}

void NetwMultiplayer::interest_set_entity_intent(
    const Ref<NetwEntity> &p_entity,
    const PackedInt64Array &p_admitted
) {
    if (p_entity.is_null()) {
        return;
    }
    interest_track_lifecycle(p_entity);
    interest_sync_record(p_entity.ptr());
    liveness_adopt(p_entity.ptr());
    interest_engine.set_intent_for_peers(
        p_entity->get_rid_handle().get_id(),
        p_admitted
    );
    interest_request_flush();
}

void NetwMultiplayer::interest_sync_live_peers() {
    HashSet<int64_t> live;
    if (has_multiplayer_peer()) {
        if (inner.is_valid()) {
            set_peer_ids(gd::api_peer_ids(inner));
        }
        const PackedInt32Array peers = reachable_peer_ids();
        for (int at = 0; at < int(peers.size()); ++at) {
            live.insert(peers[at]);
        }
    }
    if (session_get_role() == ROLE_LISTEN_SERVER) {
        live.insert(MultiplayerPeer::TARGET_PEER_SERVER);
    }
    const PackedInt64Array viewers = interest_engine.viewer_peers();
    for (int at = 0; at < int(viewers.size()); ++at) {
        live.insert(viewers[at]);
    }
    PackedInt64Array ids;
    for (const int64_t &peer_id : live) {
        ids.push_back(peer_id);
    }
    if (interest_engine.set_live_peer_ids(ids)
        && interest_compat_refresh.is_valid()) {
        interest_compat_refresh.call();
    }
}

Error NetwMultiplayer::interest_recompute() {
    NETW_ZONE_NC("NetwMultiplayer interest recompute", colors::INTEREST);
    if (!is_server()) {
        interest_pending = interest::Delta();
        interest_pending_live = false;
        return OK;
    }
    interest_sync_live_peers();
    HashSet<int64_t> slots;
    const PackedInt64Array named = interest_engine.membership_keys();
    for (int at = 0; at < int(named.size()); ++at) {
        slots.insert(named[at]);
    }
    const PackedInt64Array intents = interest_engine.intent_keys();
    for (int at = 0; at < int(intents.size()); ++at) {
        slots.insert(intents[at]);
    }
    for (const int64_t &slot : slots) {
        const Ref<NetwEntity> member = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(slot).ptr())
        );
        if (member.is_null() || member->get_owner() == nullptr) {
            continue;
        }
        if (!interest_engine.has_memberships(slot)
            && !interest_engine.has_intent(slot)) {
            interest_retire_entity(member);
            continue;
        }
        interest_sync_record(member.ptr());
    }
    interest_pending = interest_engine.recompute();
    interest_pending_live = true;
    return OK;
}

void NetwMultiplayer::interest_commit() {
    if (!interest_pending_live) {
        return;
    }
    interest_apply_delta(interest_pending);
    interest_engine.commit(interest_pending);
    wrapper_sweep_retired();
    interest_refresh_all_perception();
    interest_pending = interest::Delta();
    interest_pending_live = false;
    if (event_wants(EventPlane::INTEREST_COMMIT, 0)) {
        event_emit(
            EventPlane::INTEREST_COMMIT,
            0,
            Dictionary(),
            StringName(),
            0,
            OK,
            Dictionary()
        );
    }
}

void NetwMultiplayer::interest_refresh_compat_intents() {
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        return;
    }
    plane->get_sync_compat()->refresh_interest_intents();
}

void NetwMultiplayer::interest_send_awareness(
    int64_t p_peer,
    const Array &p_wire
) {
    send_to(
        p_peer,
        0,
        wire::builtin_channel("INTEREST_AWARENESS"),
        interest::awareness_encode(p_wire),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::interest_relay_awareness() {
    const Array drained = interest_awareness_drain();
    if (!is_server() || !interest_awareness_send.is_valid()) {
        return;
    }
    for (int at = 0; at < drained.size(); ++at) {
        const Array row = drained[at];
        const int64_t target = row[0];
        if (!interest_can_send_to(target)) {
            continue;
        }
        interest_awareness_send.call(target, row[1]);
    }
}

Error NetwMultiplayer::interest_flush_tail() {
    interest_relay_awareness();
    const Ref<MultiplayerPeer> live = get_multiplayer_peer();
    if (!has_multiplayer_peer() || live.is_null()
        || live->get_connection_status()
            == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        return OK;
    }
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns != nullptr) {
        spawns->schedule_visibility_sweep();
    }
    return OK;
}

Error NetwMultiplayer::interest_flush_immediate() {
    NETW_ERR_COND_V(
        !interest_flush.is_valid(),
        ERR_UNCONFIGURED,
        sys::INTEREST,
        "an immediate interest flush was asked for with none installed, so "
        "the committed rows would stay stale"
    );
    interest_flush.call();
    return OK;
}

StringName NetwMultiplayer::interest_flush_key() {
    return StringName("interest_visibility");
}

void NetwMultiplayer::interest_request_flush() {
    if (!interest_flush.is_valid()) {
        NETW_ERROR(
            sys::INTEREST,
            "an interest flush was requested with none installed, so the "
            "committed rows would stay stale"
        );
        return;
    }
    session_defer(interest_flush, interest_flush_key());
}

bool NetwMultiplayer::interest_flush_pending() const {
    return settle_has_key(interest_flush_key());
}

void NetwMultiplayer::interest_awareness_queue_layer(
    int64_t p_peer_id,
    int64_t p_route,
    const StringName &p_layer_id,
    int p_kind
) {
    NETW_ZONE_NC("NetwMultiplayer awareness layer edge", colors::INTEREST);
    NETW_ERR_COND(
        p_peer_id == 0,
        sys::INTEREST,
        "an awareness edge was queued for peer 0, which names no peer, so the "
        "edge would sit in the relay until a drain handed it to nobody"
    );
    NETW_ERR_COND(
        p_route == 0,
        sys::INTEREST,
        "an awareness edge was queued for no route, so no receiver could "
        "resolve the entity it names"
    );
    NETW_TRACE(
        sys::INTEREST,
        "Awareness layer edge for peer %d on route %d.",
        int(p_peer_id),
        int(p_route)
    );
    interest_relay.append(
        p_peer_id,
        interest::Awareness::layer_edge(p_route, p_layer_id, p_kind)
    );
}

void NetwMultiplayer::interest_awareness_queue_observer(
    int64_t p_peer_id,
    int64_t p_route,
    const StringName &p_layer_id,
    int64_t p_observer_peer,
    int p_kind
) {
    NETW_ZONE_NC("NetwMultiplayer awareness observer edge", colors::INTEREST);
    NETW_ERR_COND(
        p_peer_id == 0,
        sys::INTEREST,
        "an awareness edge was queued for peer 0, which names no peer, so the "
        "edge would sit in the relay until a drain handed it to nobody"
    );
    NETW_ERR_COND(
        p_route == 0,
        sys::INTEREST,
        "an awareness edge was queued for no route, so no receiver could "
        "resolve the entity it names"
    );
    NETW_TRACE(
        sys::INTEREST,
        "Awareness observer edge for peer %d naming observer %d.",
        int(p_peer_id),
        int(p_observer_peer)
    );
    interest_relay.append(
        p_peer_id,
        interest::Awareness::observer_edge(
            p_route,
            p_layer_id,
            p_observer_peer,
            p_kind
        )
    );
}

Array NetwMultiplayer::interest_awareness_drain() {
    NETW_ZONE_NC("NetwMultiplayer awareness drain", colors::INTEREST);
    Array out;
    const PackedInt64Array targets = interest_relay.targets();
    for (int at = 0; at < targets.size(); ++at) {
        const Array wire = interest_relay.wire_for(targets[at]);
        if (wire.is_empty()) {
            continue;
        }
        Array row;
        row.push_back(targets[at]);
        row.push_back(wire);
        out.push_back(row);
    }
    interest_relay.clear();
    return out;
}

void NetwMultiplayer::interest_awareness_forget(int64_t p_peer_id) {
    interest_relay.forget(p_peer_id);
}

void NetwMultiplayer::interest_awareness_clear() {
    interest_relay.clear();
}

RID NetwMultiplayer::layer_open(const StringName &p_name) {
    NETW_ERR_COND_V(
        p_name.is_empty(),
        RID(),
        sys::INTEREST,
        "an interest layer was opened with no name, so nothing could address it"
    );
    const RID *known = layer_by_name.getptr(p_name);
    if (known != nullptr) {
        return *known;
    }
    Ref<NetwInterestLayer> view;
    view.instantiate();
    view->set_layer_id(p_name);
    view->bind_session(this);
    const RID minted = layer_ledger.rid_create();
    layer_by_name[p_name] = minted;
    layer_views[minted.get_id()] = view;
    return minted;
}

RID NetwMultiplayer::interest_layer_find(const StringName &p_name) const {
    const RID *known = layer_by_name.getptr(p_name);
    return known != nullptr ? *known : RID();
}

StringName NetwMultiplayer::layer_name_of(const RID &p_layer) const {
    for (const KeyValue<StringName, RID> &row : layer_by_name) {
        if (row.value == p_layer) {
            return row.key;
        }
    }
    return StringName();
}

Ref<NetwInterestLayer> NetwMultiplayer::layer_record(const RID &p_layer) const {
    return interest_layer_view(p_layer);
}

RID NetwMultiplayer::interest_layer_create(const StringName &p_name) {
    const RID opened = layer_open(p_name);
    if (!opened.is_valid() || layer_record(opened).is_null()) {
        layer_close(opened);
        return RID();
    }
    return opened;
}

void NetwMultiplayer::interest_layer_free(const RID &p_layer) {
    layer_monitor_detach(p_layer);
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    if (record.is_null()) {
        return;
    }
    const Dictionary held = record->get_entities();
    const Array members = held.keys();
    for (int at = 0; at < members.size(); at++) {
        record->remove_entity(members[at]);
    }
    const Array viewers = record->viewer_ids();
    for (int at = 0; at < viewers.size(); at++) {
        record->remove_viewer(viewers[at]);
    }
    interest_forget_layer_row(record->get_layer_id());
    layer_drivers.erase(p_layer.get_id());
    layer_close(p_layer);
}

Error NetwMultiplayer::interest_layer_add_viewer(
    const RID &p_layer,
    int64_t p_peer
) {
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    if (record.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_peer == 0) {
        return ERR_INVALID_DATA;
    }
    record->add_viewer(p_peer);
    return OK;
}

void NetwMultiplayer::interest_layer_remove_viewer(
    const RID &p_layer,
    int64_t p_peer
) {
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    if (record.is_valid()) {
        record->remove_viewer(p_peer);
    }
}

Error NetwMultiplayer::interest_layer_add_entity(
    const RID &p_layer,
    const RID &p_entity
) {
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (record.is_null() || wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!is_host()) {
        NETW_WARN(
            sys::INTEREST,
            "an entity was added to a layer off server authority"
        );
        return ERR_UNAUTHORIZED;
    }
    const bool seated
        = record->add_entity(wrapper) || record->has_entity(wrapper);
    return seated ? OK : ERR_UNAVAILABLE;
}

void NetwMultiplayer::layer_remove_entity(
    const RID &p_layer,
    const RID &p_entity
) {
    if (!is_host()) {
        return;
    }
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (record.is_valid() && wrapper.is_valid()) {
        record->remove_entity(wrapper);
    }
}

void NetwMultiplayer::interest_layer_set_param(
    const RID &p_layer,
    LayerParam p_param,
    const Variant &p_value
) {
    const Ref<NetwInterestLayer> record = layer_record(p_layer);
    if (record.is_null()) {
        return;
    }
    switch (p_param) {
        case LAYER_PARAM_POLICY:
            record->set_policy(int(p_value));
            return;
        case LAYER_PARAM_LEAVE_POLICY:
            record->set_default_leave_policy(int(p_value));
            return;
        case LAYER_PARAM_PERCEPTION_POLICY:
            record->set_default_perception_policy(int(p_value));
            return;
        default:
            NETW_ERR(
                sys::INTEREST,
                "layer parameter {} names no layer setting",
                p_param
            );
    }
}

void NetwMultiplayer::interest_layer_set_driver_callback(
    const RID &p_layer,
    const Callable &p_callback
) {
    if (layer_record(p_layer).is_null()) {
        return;
    }
    if (p_callback.is_valid()) {
        LayerDriverRow row;
        row.layer = p_layer;
        row.drive = p_callback;
        layer_drivers.insert(p_layer.get_id(), row);
    } else {
        layer_drivers.erase(p_layer.get_id());
    }
}

Error NetwMultiplayer::run_layer_drivers() {
    NETW_ZONE_NC("NetwMultiplayer run layer drivers", colors::INTEREST);
    for (const KeyValue<int64_t, LayerDriverRow> &row : layer_drivers) {
        const Variant answered = row.value.drive.call(row.value.layer);
        if (answered.get_type() != Variant::ARRAY) {
            NETW_WARN(
                sys::INTEREST,
                "a layer driver for layer %d answered a non-array, "
                "leaving its membership untouched",
                int(row.value.layer.get_id())
            );
            return ERR_INVALID_DATA;
        }

        const Array wanted = answered;
        LocalVector<RID> desired;
        HashSet<int64_t> desired_ids;
        for (int at = 0; at < wanted.size(); at++) {
            if (wanted[at].get_type() != Variant::RID) {
                NETW_WARN(
                    sys::INTEREST,
                    "a layer driver for layer %d named a non-RID entity",
                    int(row.value.layer.get_id())
                );
                return ERR_DOES_NOT_EXIST;
            }
            const RID entity = wanted[at];
            if (entity_get_view(entity).is_null()) {
                NETW_WARN(
                    sys::INTEREST,
                    "a layer driver for layer %d named an unknown entity",
                    int(row.value.layer.get_id())
                );
                return ERR_DOES_NOT_EXIST;
            }
            desired.push_back(entity);
            desired_ids.insert(entity.get_id());
        }
        const Ref<NetwInterestLayer> record = layer_record(row.value.layer);
        if (record.is_null()) {
            continue;
        }
        const Array seated = record->get_entities().keys();
        for (int at = 0; at < seated.size(); at++) {
            const Ref<NetwEntity> held = seated[at];
            if (held.is_valid()
                && !desired_ids.has(held->get_rid_handle().get_id())) {
                record->remove_entity(held);
            }
        }
        for (const RID &entity : desired) {
            record->add_entity(entity_get_view(entity));
        }
    }
    return OK;
}

Error NetwMultiplayer::interest_flush_now() {
    const Error driven = run_layer_drivers();
    if (driven != OK) {
        return driven;
    }
    session_cancel_deferred(interest_flush_key());
    const Error folded = interest_recompute();
    if (folded != OK) {
        return folded;
    }
    interest_commit();
    return interest_flush_tail();
}

bool NetwMultiplayer::interest_admits(const RID &p_entity, int64_t p_peer) {
    const int bit = interest_peer_bit(p_peer);
    if (bit < 0) {
        return false;
    }
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() && interest_bit_admits(wrapper, bit);
}

PackedInt64Array NetwMultiplayer::interest_get_row(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() ? interest_committed_row(wrapper)
                              : PackedInt64Array();
}

String NetwMultiplayer::interest_explain(const RID &p_entity, int64_t p_peer) {
    const int bit = interest_peer_bit(p_peer);
    if (bit < 0) {
        return String("peer is not registered");
    }
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() ? interest_explain_bit(wrapper, bit)
                              : String("entity is not registered");
}

TypedArray<RID> NetwMultiplayer::interest_get_membership(const RID &p_entity) {
    TypedArray<RID> out;
    const Array named = interest_membership_ids(p_entity);
    for (int at = 0; at < named.size(); at++) {
        const StringName layer_id = named[at];
        RID layer = interest_layer_find(layer_id);
        if (!layer.is_valid()) {
            layer = interest_layer_create(layer_id);
        }
        out.push_back(layer);
    }
    return out;
}

Ref<NetwInterestLayer> NetwMultiplayer::interest_layer_view(
    const RID &p_layer
) const {
    const Ref<NetwInterestLayer> *held = layer_views.getptr(p_layer.get_id());
    return held != nullptr ? *held : Ref<NetwInterestLayer>();
}

void NetwMultiplayer::layer_close(const RID &p_layer) {
    const StringName named = layer_name_of(p_layer);
    if (!named.is_empty()) {
        layer_by_name.erase(named);
    }
    layer_views.erase(p_layer.get_id());
    layer_ledger.rid_free(p_layer);
}

void NetwMultiplayer::layer_monitor_report(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer,
    const RID &p_layer,
    bool p_inside
) {
    const Callable *watcher = layer_monitors.getptr(p_layer.get_id());
    if (watcher == nullptr || !watcher->is_valid() || p_entity.is_null()) {
        return;
    }
    watcher->call(p_inside, entity_of(p_entity->get_owner()), p_peer);
}

void NetwMultiplayer::layer_monitor_enter(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer,
    RID p_layer
) {
    layer_monitor_report(p_entity, p_peer, p_layer, true);
}

void NetwMultiplayer::layer_monitor_exit(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer,
    RID p_layer
) {
    layer_monitor_report(p_entity, p_peer, p_layer, false);
}

void NetwMultiplayer::layer_monitor_visible(
    const Ref<NetwEntity> &p_entity,
    RID p_layer
) {
    layer_monitor_report(p_entity, get_unique_id(), p_layer, true);
}

void NetwMultiplayer::layer_monitor_hidden(
    const Ref<NetwEntity> &p_entity,
    RID p_layer
) {
    layer_monitor_report(p_entity, get_unique_id(), p_layer, false);
}

void NetwMultiplayer::layer_monitor_detach(const RID &p_layer) {
    layer_monitors.erase(p_layer.get_id());
}

void NetwMultiplayer::interest_layer_set_monitor_callback(
    const RID &p_layer,
    const Callable &p_callback
) {
    layer_monitor_detach(p_layer);
    const Ref<NetwInterestLayer> view = layer_record(p_layer);
    if (view.is_null() || !p_callback.is_valid()) {
        return;
    }
    layer_monitors.insert(p_layer.get_id(), p_callback);
    connect_once(
        Signal(view.ptr(), StringName("interest_enter")),
        callable_mp(this, &NetwMultiplayer::layer_monitor_enter).bind(p_layer),
        0
    );
    connect_once(
        Signal(view.ptr(), StringName("interest_exit")),
        callable_mp(this, &NetwMultiplayer::layer_monitor_exit).bind(p_layer),
        0
    );
    connect_once(
        Signal(view.ptr(), StringName("entity_visible")),
        callable_mp(this, &NetwMultiplayer::layer_monitor_visible)
            .bind(p_layer),
        0
    );
    connect_once(
        Signal(view.ptr(), StringName("entity_hidden")),
        callable_mp(this, &NetwMultiplayer::layer_monitor_hidden).bind(p_layer),
        0
    );
}

void NetwMultiplayer::layer_forget_all() {
    layer_monitors.clear();
    layer_drivers.clear();
    layer_by_name.clear();
    layer_views.clear();
    layer_ledger.clear();
}

int64_t NetwMultiplayer::layer_driver_count() const {
    return int64_t(layer_drivers.size());
}

Ref<NetwInterestLayer> NetwMultiplayer::interest_layer(
    const StringName &p_name
) {
    return interest_layer_view(layer_open(p_name));
}

Ref<NetwInterestLayer> NetwMultiplayer::interest_layer_named(
    const StringName &p_name
) const {
    return interest_layer_view(interest_layer_find(p_name));
}

Array NetwMultiplayer::interest_layers() const {
    Array out;
    for (const KeyValue<StringName, RID> &row : layer_by_name) {
        const Ref<NetwInterestLayer> *held
            = layer_views.getptr(row.value.get_id());
        if (held != nullptr) {
            out.push_back(*held);
        }
    }
    return out;
}

interest::Facet *NetwMultiplayer::interest_facet_on(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null() || p_entity->get_record() == nullptr) {
        return nullptr;
    }
    return &p_entity->get_record()->get_interest_facet();
}

interest::Facet *NetwMultiplayer::interest_facet_of(const RID &p_entity) {
    return interest_facet_on(entity_get_view(p_entity));
}

interest::Decl *NetwMultiplayer::interest_decl_of(const RID &p_entity) {
    interest::Facet *facet = interest_facet_of(p_entity);
    if (facet == nullptr) {
        return nullptr;
    }
    interest::Decl *decl = facet->declaration();
    NETW_ERR_COND_V(
        decl == nullptr,
        nullptr,
        sys::INTEREST,
        "the interest facet of entity %d holds no declaration",
        int64_t(p_entity.get_id())
    );
    return decl;
}

void NetwMultiplayer::interest_join(
    const RID &p_entity,
    const StringName &p_layer_id
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    if (facet == nullptr || entity.is_null() || !facet->join(p_layer_id)) {
        return;
    }
    RID layer = interest_layer_find(p_layer_id);
    if (!layer.is_valid()) {
        layer = layer_open(p_layer_id);
        if (layer.is_valid() && interest_layer_view(layer).is_null()) {
            layer_close(layer);
            return;
        }
    }
    if (is_server() && layer.is_valid()) {
        liveness_adopt(entity.ptr());
        const Ref<NetwInterestLayer> record = interest_layer_view(layer);
        if (record.is_valid()) {
            record->add_entity(entity);
        }
    }
}

void NetwMultiplayer::interest_leave(
    const RID &p_entity,
    const StringName &p_layer_id
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    if (facet == nullptr || entity.is_null() || !facet->leave(p_layer_id)) {
        return;
    }
    if (is_server()) {
        const Ref<NetwInterestLayer> record
            = interest_layer_view(interest_layer_find(p_layer_id));
        if (record.is_valid()) {
            record->remove_entity(entity);
        }
    }
}

TypedArray<StringName> NetwMultiplayer::interest_layer_ids(
    const RID &p_entity
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    return facet != nullptr ? facet->layer_ids() : TypedArray<StringName>();
}

void NetwMultiplayer::interest_on_enter(
    const RID &p_entity,
    const StringName &p_layer_id,
    const Callable &p_callback
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    if (facet != nullptr) {
        facet->on_enter(p_layer_id, p_callback);
    }
}

void NetwMultiplayer::interest_on_leave(
    const RID &p_entity,
    const StringName &p_layer_id,
    const Callable &p_callback
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    if (facet != nullptr) {
        facet->on_leave(p_layer_id, p_callback);
    }
}

void NetwMultiplayer::interest_on_leave_policy(
    const RID &p_entity,
    const StringName &p_layer_id,
    LeavePolicy p_policy,
    const Callable &p_custom
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    if (facet != nullptr) {
        facet->set_leave_policy(p_layer_id, int(p_policy), p_custom);
    }
}

void NetwMultiplayer::interest_on_perception_policy(
    const RID &p_entity,
    const StringName &p_layer_id,
    PerceptionPolicy p_policy,
    const Callable &p_custom
) {
    interest::Facet *facet = interest_facet_of(p_entity);
    if (facet != nullptr) {
        if (facet->set_perception_policy(p_layer_id, int(p_policy), p_custom)) {
            interest_reapply_perception(entity_get_view(p_entity));
        }
    }
}

bool NetwMultiplayer::interest_is_filtered(const RID &p_entity) {
    if (!is_server()) {
        interest::Decl *decl = interest_decl_of(p_entity);
        return decl != nullptr && !decl->labels().is_empty();
    }
    return interest_engine.has_memberships(p_entity.get_id());
}

Array NetwMultiplayer::interest_membership_ids(const RID &p_entity) {
    LocalVector<StringName> ordered;
    const Array committed = interest_engine.memberships(p_entity.get_id());
    Array source = committed;
    if (committed.is_empty() && !is_server()) {
        interest::Decl *decl = interest_decl_of(p_entity);
        source = decl != nullptr ? decl->labels() : Array();
    }
    for (int index = 0; index < source.size(); ++index) {
        const StringName id = source[index];
        uint32_t at = ordered.size();
        while (at > 0 && String(id) < String(ordered[at - 1])) {
            --at;
        }
        ordered.insert(at, id);
    }
    Array out;
    for (uint32_t index = 0; index < ordered.size(); ++index) {
        out.push_back(ordered[index]);
    }
    return out;
}

} // namespace netw
