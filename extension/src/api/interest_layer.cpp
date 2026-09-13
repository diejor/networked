#include "netw/api/interest_layer.hpp"
#include "netw/api/interest_handle.hpp"

#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_INTEREST_ENTER = "interest_enter";
constexpr const char *SIG_INTEREST_EXIT = "interest_exit";
constexpr const char *SIG_ENTITY_VISIBLE = "entity_visible";
constexpr const char *SIG_ENTITY_HIDDEN = "entity_hidden";
constexpr const char *SIG_VIEWER_ADDED = "viewer_added";
constexpr const char *SIG_VIEWER_REMOVED = "viewer_removed";
constexpr const char *SIG_ENTITY_ADDED = "entity_added";
constexpr const char *SIG_ENTITY_REMOVED = "entity_removed";

PropertyInfo entity_param() {
    return PropertyInfo(
        Variant::OBJECT,
        "entity",
        PROPERTY_HINT_RESOURCE_TYPE,
        "NetwEntity"
    );
}

Ref<NetwInterestHandle> facet_of(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return Ref<NetwInterestHandle>();
    }
    return p_entity->get_interest();
}

} // namespace

int64_t NetwInterestLayer::slot_of(const Ref<NetwEntity> &p_entity) {
    return p_entity.is_valid() ? p_entity->get_rid_handle().get_id() : 0;
}

NetwMultiplayer *NetwInterestLayer::host() {
    return Object::cast_to<NetwMultiplayer>(session.resolve(sys::INTEREST));
}

void NetwInterestLayer::bind_session(NetwMultiplayer *p_host) {
    if (p_host == nullptr) {
        return;
    }
    session.bind(p_host);
    engine = &p_host->interest_plane();
    if (!layer_id.is_empty()) {
        engine->declare_layer(layer_id);
    }
}

void NetwInterestLayer::set_layer_id(const StringName &p_id) {
    layer_id = p_id;
    if (!layer_id.is_empty()) {
        engine->declare_layer(layer_id);
    }
}

Ref<NetwEntity> NetwInterestLayer::entity_for(int64_t p_slot) {
    NetwMultiplayer *owner = host();
    if (owner == nullptr) {
        return Ref<NetwEntity>();
    }
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(owner->wrapper_for_id(p_slot).ptr())
    );
}

bool NetwInterestLayer::server_authority() {
    NetwMultiplayer *owner = host();
    return owner == nullptr || owner->is_server();
}

int64_t NetwInterestLayer::local_peer_id() {
    NetwMultiplayer *owner = host();
    if (owner == nullptr || !owner->has_multiplayer_peer()) {
        return 1;
    }
    return owner->get_unique_id();
}

void NetwInterestLayer::dispatch_enter(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    const Ref<NetwInterestHandle> facet = facet_of(p_entity);
    if (facet.is_valid()) {
        facet->dispatch_enter(layer_id, p_peer_id);
    }
}

void NetwInterestLayer::dispatch_leave(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    const Ref<NetwInterestHandle> facet = facet_of(p_entity);
    if (facet.is_valid()) {
        facet->dispatch_leave(layer_id, p_peer_id);
    }
}

void NetwInterestLayer::join_label(const Ref<NetwEntity> &p_entity) {
    const Ref<NetwInterestHandle> facet = facet_of(p_entity);
    if (facet.is_valid()) {
        facet->client_join_label(layer_id);
    }
}

int64_t NetwInterestLayer::adopt_slot(const Ref<NetwEntity> &p_entity) {
    NetwMultiplayer *owner = host();
    if (owner != nullptr && p_entity.is_valid()) {
        owner->liveness_adopt(p_entity.ptr());
    }
    return slot_of(p_entity);
}

void NetwInterestLayer::track_membership(
    const Ref<NetwEntity> &p_entity,
    bool p_joined
) {
    NetwMultiplayer *owner = host();
    if (owner == nullptr) {
        return;
    }
    if (p_joined) {
        engine->membership_add(adopt_slot(p_entity), layer_id);
        owner->interest_track_lifecycle(p_entity);
    } else {
        engine->membership_remove(slot_of(p_entity), layer_id);
    }
    const int64_t slot = slot_of(p_entity);
    if (!engine->has_memberships(slot) && !engine->has_intent(slot)) {
        if (slot != 0 && engine->has_entity(slot)) {
            engine->remove_entity(slot);
        }
    } else {
        owner->interest_sync_record(p_entity.ptr());
    }
    request_flush();
}

void NetwInterestLayer::request_flush() {
    NetwMultiplayer *owner = host();
    if (owner != nullptr) {
        owner->interest_request_flush();
    }
}

void NetwInterestLayer::refresh_perception(const Ref<NetwEntity> &p_entity) {
    NetwMultiplayer *owner = host();
    if (owner == nullptr) {
        return;
    }
    Array layers;
    layers.push_back(layer_id);
    owner->interest_refresh_perception(p_entity, layers);
}

int NetwInterestLayer::get_policy() const {
    return engine->layer_policy(layer_id);
}

bool NetwInterestLayer::set_policy(int p_policy) {
    if (!engine->layer_set_policy(layer_id, p_policy)) {
        return false;
    }
    request_flush();
    return true;
}

int NetwInterestLayer::get_default_leave_policy() const {
    return engine->layer_leave_policy(layer_id);
}

void NetwInterestLayer::set_default_leave_policy(int p_policy) {
    engine->layer_set_leave_policy(layer_id, p_policy);
}

int NetwInterestLayer::get_default_perception_policy() const {
    return engine->layer_perception_policy(layer_id);
}

void NetwInterestLayer::set_default_perception_policy(int p_policy) {
    if (!engine->layer_set_perception_policy(layer_id, p_policy)) {
        return;
    }
    NetwMultiplayer *owner = host();
    if (owner == nullptr) {
        return;
    }
    const PackedInt64Array slots = engine->roster(layer_id);
    for (int at = 0; at < int(slots.size()); ++at) {
        const Ref<NetwEntity> member = entity_for(slots[at]);
        if (member.is_valid()) {
            owner->interest_reapply_perception(member);
        }
    }
}

Dictionary NetwInterestLayer::get_viewers() const {
    Dictionary out;
    const PackedInt64Array peers = engine->layer_viewers(layer_id);
    for (int at = 0; at < int(peers.size()); ++at) {
        out[peers[at]] = true;
    }
    return out;
}

Dictionary NetwInterestLayer::get_entities() {
    Dictionary out;
    if (host() == nullptr) {
        return out;
    }
    const PackedInt64Array slots = engine->roster(layer_id);
    for (int at = 0; at < int(slots.size()); ++at) {
        const Ref<NetwEntity> member = entity_for(slots[at]);
        if (member.is_valid()) {
            out[member] = true;
        }
    }
    return out;
}

bool NetwInterestLayer::add_viewer(int64_t p_peer_id) {
    NetwMultiplayer *owner = host();
    if (owner != nullptr && owner->has_multiplayer_peer()
        && p_peer_id != godot::MultiplayerPeer::TARGET_PEER_SERVER
        && owner->interest_peer_bit(p_peer_id) < 0) {
        NETW_WARN(
            sys::INTEREST,
            "layer '%s' admits peer %d, which this session has never seen. "
            "A peer id is minted by the transport at connect, so admit from "
            "the peer_connected handler or from a peer this session lists, "
            "never from a loop index or a game-side player id.",
            String(layer_id).utf8().get_data(),
            int(p_peer_id)
        );
    }
    if (!engine->layer_add_viewer(layer_id, p_peer_id)) {
        return false;
    }
    emit_signal(SIG_VIEWER_ADDED, p_peer_id);
    request_flush();
    return true;
}

bool NetwInterestLayer::remove_viewer(int64_t p_peer_id) {
    if (!engine->layer_remove_viewer(layer_id, p_peer_id)) {
        return false;
    }
    emit_signal(SIG_VIEWER_REMOVED, p_peer_id);
    request_flush();
    return true;
}

bool NetwInterestLayer::has_viewer(int64_t p_peer_id) const {
    return engine->layer_has_viewer(layer_id, p_peer_id);
}

bool NetwInterestLayer::add_entity(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND_V(
        p_entity.is_null(),
        false,
        sys::INTEREST,
        "layer %s was asked to admit no entity",
        String(layer_id)
    );
    if (!server_authority()) {
        return false;
    }
    if (!engine->roster_add(layer_id, slot_of(p_entity))) {
        return false;
    }
    emit_signal(SIG_ENTITY_ADDED, p_entity);
    track_membership(p_entity, true);
    return true;
}

bool NetwInterestLayer::remove_entity(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND_V(
        p_entity.is_null(),
        false,
        sys::INTEREST,
        "layer %s was asked to drop no entity",
        String(layer_id)
    );
    if (!server_authority()) {
        return false;
    }
    if (!engine->roster_remove(layer_id, slot_of(p_entity))) {
        return false;
    }
    emit_signal(SIG_ENTITY_REMOVED, p_entity);
    track_membership(p_entity, false);
    return true;
}

bool NetwInterestLayer::has_entity(const Ref<NetwEntity> &p_entity) const {
    return engine->roster_has(layer_id, slot_of(p_entity));
}

void NetwInterestLayer::client_admit(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND(
        p_entity.is_null(),
        sys::INTEREST,
        "layer %s was asked to admit no entity on a client",
        String(layer_id)
    );
    if (!engine->roster_add(layer_id, slot_of(p_entity))) {
        return;
    }
    track_membership(p_entity, true);
    join_label(p_entity);
    dispatch_enter(p_entity, local_peer_id());
    emit_signal(SIG_ENTITY_VISIBLE, p_entity);
    refresh_perception(p_entity);
}

void NetwInterestLayer::client_revoke(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND(
        p_entity.is_null(),
        sys::INTEREST,
        "layer %s was asked to revoke no entity on a client",
        String(layer_id)
    );
    if (!engine->roster_remove(layer_id, slot_of(p_entity))) {
        return;
    }
    dispatch_leave(p_entity, local_peer_id());
    emit_signal(SIG_ENTITY_HIDDEN, p_entity);
    refresh_perception(p_entity);
}

void NetwInterestLayer::client_untrack_entity(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND(
        p_entity.is_null(),
        sys::INTEREST,
        "layer %s was asked to untrack no entity on a client",
        String(layer_id)
    );
    if (!engine->roster_remove(layer_id, slot_of(p_entity))) {
        return;
    }
    emit_signal(SIG_ENTITY_REMOVED, p_entity);
    track_membership(p_entity, false);
    dispatch_leave(p_entity, local_peer_id());
    emit_signal(SIG_ENTITY_HIDDEN, p_entity);
    refresh_perception(p_entity);
}

void NetwInterestLayer::apply_server_transition(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id,
    bool p_visible
) {
    engine->note_transition(layer_id);
    if (p_visible) {
        emit_signal(SIG_INTEREST_ENTER, p_entity, p_peer_id);
        if (p_entity.is_valid()) {
            p_entity->emit_signal(SIG_INTEREST_ENTER, p_peer_id);
        }
        dispatch_enter(p_entity, p_peer_id);
        return;
    }
    emit_signal(SIG_INTEREST_EXIT, p_entity, p_peer_id);
    if (p_entity.is_valid()) {
        p_entity->emit_signal(SIG_INTEREST_EXIT, p_peer_id);
    }
    dispatch_leave(p_entity, p_peer_id);
}

bool NetwInterestLayer::is_visible_to(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    NetwMultiplayer *owner = host();
    if (owner != nullptr) {
        return owner->interest_participant_sees(p_peer_id, p_entity);
    }
    return has_entity(p_entity) && verdict_for(p_peer_id);
}

bool NetwInterestLayer::verdict_for(int64_t p_peer_id) const {
    return engine->layer_admits(layer_id, p_peer_id);
}

Array NetwInterestLayer::viewer_ids() const {
    Array out;
    const PackedInt64Array peers = engine->layer_viewers(layer_id);
    for (int at = 0; at < int(peers.size()); ++at) {
        out.push_back(peers[at]);
    }
    return out;
}

Dictionary NetwInterestLayer::monitor_snapshot() const {
    Dictionary out;
    out[StringName("viewers")] = engine->layer_viewers(layer_id).size();
    out[StringName("entities")] = engine->roster(layer_id).size();
    out[StringName("visible_edges")] = engine->layer_edge_count(layer_id);
    out[StringName("transitions_total")] = engine->transitions(layer_id);
    return out;
}

Dictionary NetwInterestLayer::debug_dump(int64_t p_peer_id) {
    Dictionary out;
    out["layer_id"] = String(layer_id);
    out["policy"] = get_policy();
    out["viewers"] = viewer_ids();
    out["entities"] = engine->roster(layer_id).size();
    out["peer_id"] = p_peer_id;
    out["verdict"] = verdict_for(p_peer_id);
    out["explanation"] = engine->layer_explain(layer_id, p_peer_id);
    return out;
}

void NetwInterestLayer::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_layer_id", "id"),
        &NetwInterestLayer::set_layer_id
    );
    ClassDB::bind_method(
        D_METHOD("get_layer_id"),
        &NetwInterestLayer::get_layer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "layer_id"),
        "set_layer_id",
        "get_layer_id"
    );

    ClassDB::bind_method(
        D_METHOD("set_policy", "value"),
        &NetwInterestLayer::set_policy
    );
    ClassDB::bind_method(
        D_METHOD("get_policy"),
        &NetwInterestLayer::get_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "policy",
            PROPERTY_HINT_ENUM,
            "Hide From Outsiders,Hide From Insiders"
        ),
        "set_policy",
        "get_policy"
    );

    ClassDB::bind_method(
        D_METHOD("set_default_leave_policy", "value"),
        &NetwInterestLayer::set_default_leave_policy
    );
    ClassDB::bind_method(
        D_METHOD("get_default_leave_policy"),
        &NetwInterestLayer::get_default_leave_policy
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "default_leave_policy"),
        "set_default_leave_policy",
        "get_default_leave_policy"
    );

    ClassDB::bind_method(
        D_METHOD("set_default_perception_policy", "value"),
        &NetwInterestLayer::set_default_perception_policy
    );
    ClassDB::bind_method(
        D_METHOD("get_default_perception_policy"),
        &NetwInterestLayer::get_default_perception_policy
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "default_perception_policy"),
        "set_default_perception_policy",
        "get_default_perception_policy"
    );

    ClassDB::bind_method(
        D_METHOD("get_viewers"),
        &NetwInterestLayer::get_viewers
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "viewers"),
        "",
        "get_viewers"
    );

    ClassDB::bind_method(
        D_METHOD("get_entities"),
        &NetwInterestLayer::get_entities
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "entities"),
        "",
        "get_entities"
    );

    ClassDB::bind_method(
        D_METHOD("add_viewer", "peer_id"),
        &NetwInterestLayer::add_viewer
    );
    ClassDB::bind_method(
        D_METHOD("remove_viewer", "peer_id"),
        &NetwInterestLayer::remove_viewer
    );
    ClassDB::bind_method(
        D_METHOD("has_viewer", "peer_id"),
        &NetwInterestLayer::has_viewer
    );
    ClassDB::bind_method(
        D_METHOD("add_entity", "entity"),
        &NetwInterestLayer::add_entity
    );
    ClassDB::bind_method(
        D_METHOD("remove_entity", "entity"),
        &NetwInterestLayer::remove_entity
    );
    ClassDB::bind_method(
        D_METHOD("has_entity", "entity"),
        &NetwInterestLayer::has_entity
    );
    ClassDB::bind_method(
        D_METHOD("client_admit", "entity"),
        &NetwInterestLayer::client_admit
    );
    ClassDB::bind_method(
        D_METHOD("client_revoke", "entity"),
        &NetwInterestLayer::client_revoke
    );
    ClassDB::bind_method(
        D_METHOD("client_untrack_entity", "entity"),
        &NetwInterestLayer::client_untrack_entity
    );
    ClassDB::bind_method(
        D_METHOD("apply_server_transition", "entity", "peer_id", "visible"),
        &NetwInterestLayer::apply_server_transition
    );
    ClassDB::bind_method(
        D_METHOD("is_visible_to", "entity", "peer_id"),
        &NetwInterestLayer::is_visible_to
    );
    ClassDB::bind_method(
        D_METHOD("verdict_for", "peer_id"),
        &NetwInterestLayer::verdict_for
    );
    ClassDB::bind_method(
        D_METHOD("viewer_ids"),
        &NetwInterestLayer::viewer_ids
    );
    ClassDB::bind_method(
        D_METHOD("monitor_snapshot"),
        &NetwInterestLayer::monitor_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("debug_dump", "peer_id"),
        &NetwInterestLayer::debug_dump,
        DEFVAL(0)
    );

    ADD_SIGNAL(MethodInfo(
        SIG_INTEREST_ENTER,
        entity_param(),
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_INTEREST_EXIT,
        entity_param(),
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(SIG_ENTITY_VISIBLE, entity_param()));
    ADD_SIGNAL(MethodInfo(SIG_ENTITY_HIDDEN, entity_param()));
    ADD_SIGNAL(
        MethodInfo(SIG_VIEWER_ADDED, PropertyInfo(Variant::INT, "peer_id"))
    );
    ADD_SIGNAL(
        MethodInfo(SIG_VIEWER_REMOVED, PropertyInfo(Variant::INT, "peer_id"))
    );
    ADD_SIGNAL(MethodInfo(SIG_ENTITY_ADDED, entity_param()));
    ADD_SIGNAL(MethodInfo(SIG_ENTITY_REMOVED, entity_param()));
}

} // namespace netw
