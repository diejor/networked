#include "netw/api/interest_handle.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

void NetwInterestHandle::bind(NetwEntity *p_entity) {
    if (p_entity == nullptr || entity_id == gd::instance_id(p_entity)) {
        return;
    }
    entity_id = gd::instance_id(p_entity);
    Node *root = p_entity->get_owner();
    if (root == nullptr) {
        return;
    }
    const Callable enter = callable_mp(this, &NetwInterestHandle::activate);
    const Callable exit = callable_mp(this, &NetwInterestHandle::deactivate);
    if (!root->is_connected("tree_entered", enter)) {
        root->connect("tree_entered", enter);
    }
    if (!root->is_connected("tree_exiting", exit)) {
        root->connect("tree_exiting", exit);
    }
    const Callable observed
        = callable_mp(this, &NetwInterestHandle::on_observer_entered);
    const Callable unobserved
        = callable_mp(this, &NetwInterestHandle::on_observer_left);
    if (!p_entity->is_connected("observer_entered", observed)) {
        p_entity->connect("observer_entered", observed);
    }
    if (!p_entity->is_connected("observer_left", unobserved)) {
        p_entity->connect("observer_left", unobserved);
    }
    if (root->is_inside_tree()) {
        activate();
    }
}

Ref<NetwEntity> NetwInterestHandle::entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(gd::object_of(entity_id))
    );
}

NetwMultiplayer *NetwInterestHandle::core() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? NetwEntity::session_core_for(bound->get_owner())
                            : nullptr;
}

NetwMultiplayer *NetwInterestHandle::attached_core() const {
    NetwMultiplayer *session = core();
    const Ref<NetwEntity> bound = entity();
    if (session == nullptr || bound.is_null()) {
        return nullptr;
    }
    session->liveness_adopt(bound.ptr());
    return session->entity_get_view(bound->get_rid_handle()).is_valid()
        ? session
        : nullptr;
}

interest::Facet *NetwInterestHandle::facet() const {
    const Ref<NetwEntity> bound = entity();
    if (bound.is_null() || bound->get_record() == nullptr) {
        return nullptr;
    }
    return &bound->get_record()->get_interest_facet();
}

interest::Decl *NetwInterestHandle::declaration() {
    interest::Facet *held = facet();
    return held != nullptr ? held->declaration() : nullptr;
}

bool NetwInterestHandle::is_authority() const {
    NetwMultiplayer *session = core();
    return session != nullptr && session->is_server();
}

RID NetwInterestHandle::layer_ensure(const StringName &p_layer_id) {
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return RID();
    }
    const RID known = session->interest_layer_find(p_layer_id);
    if (known.is_valid()) {
        return known;
    }
    const RID opened = session->layer_open(p_layer_id);
    if (opened.is_valid() && session->interest_layer_view(opened).is_null()) {
        session->layer_close(opened);
        return RID();
    }
    return opened;
}

void NetwInterestHandle::layer_join_live(const StringName &p_layer_id) {
    const Ref<NetwEntity> bound = entity();
    NetwMultiplayer *session = core();
    if (session == nullptr || bound.is_null()) {
        return;
    }
    const RID layer = layer_ensure(p_layer_id);
    if (!is_authority() || !layer.is_valid()) {
        return;
    }
    session->liveness_adopt(bound.ptr());
    const Ref<NetwInterestLayer> record = session->interest_layer_view(layer);
    if (record.is_valid()) {
        record->add_entity(bound);
    }
}

void NetwInterestHandle::layer_leave_live(const StringName &p_layer_id) {
    const Ref<NetwEntity> bound = entity();
    NetwMultiplayer *session = core();
    if (session == nullptr || bound.is_null() || !is_authority()) {
        return;
    }
    const Ref<NetwInterestLayer> record = session->interest_layer_view(
        session->interest_layer_find(p_layer_id)
    );
    if (record.is_valid()) {
        record->remove_entity(bound);
    }
}

Ref<NetwInterestHandle> NetwInterestHandle::of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::ensure(p_node);
    if (entity.is_null()) {
        NETW_ERROR(
            sys::INTEREST,
            "NetwInterestHandle.of: the node roots no entity"
        );
        return Ref<NetwInterestHandle>();
    }
    return entity->get_interest();
}

Ref<NetwInterestHandle> NetwInterestHandle::join(
    const StringName &p_layer_id,
    int64_t p_leave_policy,
    int64_t p_perception_policy
) {
    NetwMultiplayer *session = attached_core();
    const Ref<NetwEntity> bound = entity();
    interest::Facet *held = facet();
    if (session != nullptr && bound.is_valid()) {
        session->interest_join(bound->get_rid_handle(), p_layer_id);
    } else if (held != nullptr) {
        held->join(p_layer_id);
    }
    if (p_leave_policy >= 0) {
        on_leave_policy(
            p_layer_id,
            static_cast<NetwMultiplayer::LeavePolicy>(p_leave_policy),
            Callable()
        );
    }
    if (p_perception_policy >= 0) {
        on_perception_policy(
            p_layer_id,
            static_cast<NetwMultiplayer::PerceptionPolicy>(p_perception_policy),
            Callable()
        );
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::leave(
    const StringName &p_layer_id
) {
    NetwMultiplayer *session = attached_core();
    const Ref<NetwEntity> bound = entity();
    interest::Facet *held = facet();
    if (session != nullptr && bound.is_valid()) {
        session->interest_leave(bound->get_rid_handle(), p_layer_id);
    } else if (held != nullptr) {
        held->leave(p_layer_id);
    }
    return Ref<NetwInterestHandle>(this);
}

TypedArray<StringName> NetwInterestHandle::layer_ids() const {
    interest::Facet *held = facet();
    return held != nullptr ? held->layer_ids() : TypedArray<StringName>();
}

bool NetwInterestHandle::is_visible_to(int64_t p_peer_id) const {
    const Ref<NetwEntity> bound = entity();
    NetwMultiplayer *session = core();
    if (session == nullptr || bound.is_null()) {
        return false;
    }
    if (!session->interest_is_filtered(bound->get_rid_handle())) {
        return true;
    }
    const int bit = session->interest_peer_bit(p_peer_id);
    return bit >= 0 && session->interest_bit_admits(bound, bit);
}

Ref<NetwInterestHandle> NetwInterestHandle::bind_layer_callback(
    const Callable &p_callback,
    const StringName &p_layer_id,
    bool p_on_enter
) {
    NetwMultiplayer *session = attached_core();
    const Ref<NetwEntity> bound = entity();
    interest::Facet *held = facet();
    if (session != nullptr && bound.is_valid()) {
        if (p_on_enter) {
            session->interest_on_enter(
                bound->get_rid_handle(),
                p_layer_id,
                p_callback
            );
        } else {
            session->interest_on_leave(
                bound->get_rid_handle(),
                p_layer_id,
                p_callback
            );
        }
    } else if (held != nullptr) {
        if (p_on_enter) {
            held->on_enter(p_layer_id, p_callback);
        } else {
            held->on_leave(p_layer_id, p_callback);
        }
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_enter(
    const Callable &p_callback,
    const StringName &p_layer_id
) {
    if (!String(p_layer_id).is_empty()) {
        return bind_layer_callback(p_callback, p_layer_id, true);
    }
    const TypedArray<StringName> joined = layer_ids();
    for (int at = 0; at < joined.size(); ++at) {
        bind_layer_callback(p_callback, StringName(joined[at]), true);
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_leave(
    const Callable &p_callback,
    const StringName &p_layer_id
) {
    if (!String(p_layer_id).is_empty()) {
        return bind_layer_callback(p_callback, p_layer_id, false);
    }
    const TypedArray<StringName> joined = layer_ids();
    for (int at = 0; at < joined.size(); ++at) {
        bind_layer_callback(p_callback, StringName(joined[at]), false);
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_observed(
    const Callable &p_callback
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->on_observed(p_callback);
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_unobserved(
    const Callable &p_callback
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->on_unobserved(p_callback);
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_leave_policy(
    const StringName &p_layer_id,
    NetwMultiplayer::LeavePolicy p_policy,
    const Callable &p_custom
) {
    NetwMultiplayer *session = attached_core();
    const Ref<NetwEntity> bound = entity();
    interest::Facet *held = facet();
    if (session != nullptr && bound.is_valid()) {
        session->interest_on_leave_policy(
            bound->get_rid_handle(),
            p_layer_id,
            p_policy,
            p_custom
        );
    } else if (held != nullptr) {
        held->set_leave_policy(p_layer_id, int(p_policy), p_custom);
    }
    return Ref<NetwInterestHandle>(this);
}

Ref<NetwInterestHandle> NetwInterestHandle::on_perception_policy(
    const StringName &p_layer_id,
    NetwMultiplayer::PerceptionPolicy p_policy,
    const Callable &p_custom
) {
    interest::Facet *held = facet();
    const Ref<NetwEntity> bound = entity();
    NetwMultiplayer *session = attached_core();
    if (session != nullptr && bound.is_valid()) {
        session->interest_on_perception_policy(
            bound->get_rid_handle(),
            p_layer_id,
            p_policy,
            p_custom
        );
    } else if (held != nullptr) {
        held->set_perception_policy(p_layer_id, int(p_policy), p_custom);
    }
    return Ref<NetwInterestHandle>(this);
}

void NetwInterestHandle::set_report_observers(bool p_enabled) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->declaration()->set_reports_observers(p_enabled);
    }
}

bool NetwInterestHandle::reports_observers() const {
    interest::Facet *held = facet();
    return held != nullptr && held->declaration()->get_reports_observers();
}

int64_t NetwInterestHandle::leave_policy_for(
    const StringName &p_layer_id,
    int64_t p_fallback
) const {
    interest::Facet *held = facet();
    return held != nullptr
        ? held->declaration()->leave_policy_for(p_layer_id, int(p_fallback))
        : p_fallback;
}

Callable NetwInterestHandle::custom_leave_for(
    const StringName &p_layer_id
) const {
    interest::Facet *held = facet();
    return held != nullptr ? held->declaration()->custom_leave_for(p_layer_id)
                           : Callable();
}

int64_t NetwInterestHandle::perception_policy_for(
    const StringName &p_layer_id,
    int64_t p_fallback
) const {
    interest::Facet *held = facet();
    return held != nullptr ? held->declaration()->perception_policy_for(
                                 p_layer_id,
                                 int(p_fallback)
                             )
                           : p_fallback;
}

Callable NetwInterestHandle::custom_perception_for(
    const StringName &p_layer_id
) const {
    interest::Facet *held = facet();
    return held != nullptr
        ? held->declaration()->custom_perception_for(p_layer_id)
        : Callable();
}

void NetwInterestHandle::client_join_label(const StringName &p_layer_id) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->join(p_layer_id);
    }
}

void NetwInterestHandle::dispatch_enter(
    const StringName &p_layer_id,
    int64_t p_peer
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->dispatch_enter(p_layer_id, p_peer);
    }
}

void NetwInterestHandle::dispatch_leave(
    const StringName &p_layer_id,
    int64_t p_peer
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->dispatch_leave(p_layer_id, p_peer);
    }
}

void NetwInterestHandle::activate() {
    const Ref<NetwEntity> bound = entity();
    if (bound.is_null() || core() == nullptr) {
        return;
    }
    interest::Facet *held = facet();
    if (held == nullptr) {
        return;
    }
    const Array labels = held->declaration()->labels();
    for (int at = 0; at < labels.size(); at++) {
        layer_join_live(labels[at]);
    }
}

void NetwInterestHandle::deactivate() {
    interest::Facet *held = facet();
    if (held == nullptr) {
        return;
    }
    const Array labels = held->declaration()->labels();
    for (int at = 0; at < labels.size(); at++) {
        layer_leave_live(labels[at]);
    }
}

void NetwInterestHandle::on_observer_entered(
    const StringName &p_layer_id,
    int64_t p_peer
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->dispatch_observed(p_peer);
    }
}

void NetwInterestHandle::on_observer_left(
    const StringName &p_layer_id,
    int64_t p_peer
) {
    interest::Facet *held = facet();
    if (held != nullptr) {
        held->dispatch_unobserved(p_peer);
    }
}

void NetwInterestHandle::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwInterestHandle",
        D_METHOD("of", "node"),
        &NetwInterestHandle::of
    );
    ClassDB::bind_method(
        D_METHOD("join", "layer_id", "leave_policy", "perception_policy"),
        &NetwInterestHandle::join,
        DEFVAL(-1),
        DEFVAL(-1)
    );
    ClassDB::bind_method(
        D_METHOD("leave", "layer_id"),
        &NetwInterestHandle::leave
    );
    ClassDB::bind_method(D_METHOD("layer_ids"), &NetwInterestHandle::layer_ids);
    ClassDB::bind_method(
        D_METHOD("on_enter", "callback", "layer_id"),
        &NetwInterestHandle::on_enter,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("on_leave", "callback", "layer_id"),
        &NetwInterestHandle::on_leave,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("on_leave_policy", "layer_id", "policy", "custom_callback"),
        &NetwInterestHandle::on_leave_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD(
            "on_perception_policy",
            "layer_id",
            "policy",
            "custom_callback"
        ),
        &NetwInterestHandle::on_perception_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(D_METHOD("entity"), &NetwInterestHandle::entity);
}

Ref<NetwInterestHandle> build_interest_handle(Object *p_entity) {
    Ref<NetwInterestHandle> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw
