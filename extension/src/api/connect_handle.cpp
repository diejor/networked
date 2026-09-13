#include "netw/api/connect_handle.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/transport.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_ENDPOINT_ADDED = "endpoint_added";
const char *SIG_JOIN_FAILED = "join_failed";
const char *SIG_ENDPOINT_REMOVED = "endpoint_removed";
const char *SIG_ENDPOINT_UPDATED = "endpoint_updated";

} // namespace

NetwMultiplayer *NetwConnectHandle::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwConnectHandle::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session == nullptr) {
        return;
    }
    p_session->connect(
        StringName("session_join_failed"),
        callable_mp(this, &NetwConnectHandle::relay_join_failed)
    );
    p_session->connect(
        StringName("endpoint_added"),
        callable_mp(this, &NetwConnectHandle::relay_endpoint_added)
    );
    p_session->connect(
        StringName("endpoint_removed"),
        callable_mp(this, &NetwConnectHandle::relay_endpoint_removed)
    );
    p_session->connect(
        StringName("endpoint_updated"),
        callable_mp(this, &NetwConnectHandle::relay_endpoint_updated)
    );
    for (const RID &target : p_session->endpoint_list()) {
        remember_endpoint(target);
    }
}

void NetwConnectHandle::relay_join_failed(
    int64_t p_error,
    const String &p_reason
) {
    emit_signal(StringName(SIG_JOIN_FAILED), p_error, p_reason);
}

void NetwConnectHandle::remember_endpoint(const RID &p_target) {
    if (endpoint_keys.has(p_target)) {
        return;
    }
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    const RID transport_rid = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_TRANSPORT
    );
    const StringName peer_class = api->transport_class_of(transport_rid);
    if (peer_class.is_empty()) {
        return;
    }
    const String address = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_ADDRESS
    );
    endpoint_keys[p_target] = EndpointKey{peer_class, address};
}

void NetwConnectHandle::relay_endpoint_added(const RID &p_target) {
    remember_endpoint(p_target);
    const HashMap<RID, EndpointKey>::ConstIterator found
        = endpoint_keys.find(p_target);
    if (found == endpoint_keys.end()) {
        return;
    }
    emit_signal(
        StringName(SIG_ENDPOINT_ADDED),
        found->value.peer_class,
        found->value.address
    );
}

void NetwConnectHandle::relay_endpoint_removed(const RID &p_target) {
    const HashMap<RID, EndpointKey>::ConstIterator found
        = endpoint_keys.find(p_target);
    if (found == endpoint_keys.end()) {
        return;
    }
    const EndpointKey key = found->value;
    endpoint_keys.erase(p_target);
    emit_signal(StringName(SIG_ENDPOINT_REMOVED), key.peer_class, key.address);
}

void NetwConnectHandle::relay_endpoint_updated(const RID &p_target) {
    remember_endpoint(p_target);
    const HashMap<RID, EndpointKey>::ConstIterator found
        = endpoint_keys.find(p_target);
    if (found == endpoint_keys.end()) {
        return;
    }
    emit_signal(
        StringName(SIG_ENDPOINT_UPDATED),
        found->value.peer_class,
        found->value.address
    );
}

RID NetwConnectHandle::create_peer(
    const Variant &p_transport,
    int64_t p_mode,
    const String &p_address,
    const Dictionary &p_settings,
    const Callable &p_completed,
    const Callable &p_progress
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return RID();
    }
    return api->transport_create_peer(
        resolve_transport(p_transport),
        p_mode,
        p_address,
        p_settings,
        p_completed,
        p_progress
    );
}

void NetwConnectHandle::cancel_peer_creation(const RID &p_ticket) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->transport_cancel_peer_creation(p_ticket);
    }
}

String NetwConnectHandle::get_join_address() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_join_address() : String();
}

Dictionary NetwConnectHandle::diagnostics(int64_t p_peer_id) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_diagnostics(p_peer_id) : Dictionary();
}

RID NetwConnectHandle::resolve_transport(const Variant &p_of) const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return RID();
    }
    if (p_of.get_type() == Variant::RID) {
        return p_of;
    }
    const Ref<Script> as_script = p_of;
    if (as_script.is_valid()) {
        const RID registered = api->transport_find_script(as_script);
        if (registered.is_valid()) {
            return registered;
        }
    }
    return api->transport_find(connect::peer_class_of_type(p_of));
}

RID NetwConnectHandle::resolve_endpoint(
    const Variant &p_transport,
    const String &p_address
) const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return RID();
    }
    const RID transport_rid = resolve_transport(p_transport);
    if (!transport_rid.is_valid()) {
        return RID();
    }
    return api->endpoint_find(transport_rid, p_address);
}

Dictionary NetwConnectHandle::transport_snapshot(const RID &p_transport) const {
    Dictionary snapshot;
    NetwMultiplayer *api = session();
    if (api == nullptr || !p_transport.is_valid()) {
        return snapshot;
    }
    const StringName peer_class = api->transport_class_of(p_transport);
    if (peer_class.is_empty()) {
        return snapshot;
    }
    snapshot["peer_class"] = peer_class;
    snapshot["display_name"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
    );
    snapshot["address_label"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_ADDRESS_LABEL
    );
    snapshot["address_placeholder"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_ADDRESS_PLACEHOLDER
    );
    snapshot["address_help"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_ADDRESS_HELP
    );
    snapshot["capabilities"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_CAPABILITIES
    );
    snapshot["host_settings"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_HOST_SETTINGS
    );
    snapshot["client_settings"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_CLIENT_SETTINGS
    );
    return snapshot;
}

Dictionary NetwConnectHandle::endpoint_snapshot(const RID &p_target) const {
    Dictionary snapshot;
    NetwMultiplayer *api = session();
    if (api == nullptr || !p_target.is_valid()) {
        return snapshot;
    }
    const RID transport_rid = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_TRANSPORT
    );
    const StringName peer_class = api->transport_class_of(transport_rid);
    if (peer_class.is_empty()) {
        return snapshot;
    }
    const int64_t flags = api->endpoint_get_state(
        p_target,
        NetwMultiplayer::ENDPOINT_STATE_FLAGS
    );
    snapshot["peer_class"] = peer_class;
    snapshot["address"] = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_ADDRESS
    );
    snapshot["display_name"] = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_DISPLAY_NAME
    );
    const Variant status = api->endpoint_get_state(
        p_target,
        NetwMultiplayer::ENDPOINT_STATE_STATUS
    );
    snapshot["status"]
        = status.get_type() == Variant::NIL ? Variant(int64_t(FAILED)) : status;
    snapshot["info"] = api->endpoint_get_state(
        p_target,
        NetwMultiplayer::ENDPOINT_STATE_INFO
    );
    snapshot["is_caller_added"]
        = (flags & NetwMultiplayer::ENDPOINT_FLAG_CALLER) != 0;
    snapshot["is_available"]
        = (flags & NetwMultiplayer::ENDPOINT_FLAG_AVAILABLE) != 0;
    snapshot["is_observed"]
        = (flags & NetwMultiplayer::ENDPOINT_FLAG_OBSERVED) != 0;
    return snapshot;
}

Array NetwConnectHandle::transports() const {
    NetwMultiplayer *api = session();
    Array snapshots;
    if (api == nullptr) {
        return snapshots;
    }
    for (const Variant &entry : api->transport_list()) {
        snapshots.push_back(transport_snapshot(entry));
    }
    return snapshots;
}

Dictionary NetwConnectHandle::transport(const Variant &p_of) const {
    return transport_snapshot(resolve_transport(p_of));
}

bool NetwConnectHandle::register_transport(const Ref<Script> &p_type) {
    NetwMultiplayer *api = session();
    return api != nullptr && api->transport_register(p_type).is_valid();
}

bool NetwConnectHandle::unregister_transport(const Variant &p_of) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return false;
    }
    return api->transport_unregister(resolve_transport(p_of)) == OK;
}

Array NetwConnectHandle::join_schema() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_join_schema() : Array();
}

Array NetwConnectHandle::endpoints() const {
    NetwMultiplayer *api = session();
    Array snapshots;
    if (api == nullptr) {
        return snapshots;
    }
    for (const Variant &entry : api->endpoint_list()) {
        snapshots.push_back(endpoint_snapshot(entry));
    }
    return snapshots;
}

Dictionary NetwConnectHandle::endpoint(
    const Variant &p_transport,
    const String &p_address
) const {
    return endpoint_snapshot(resolve_endpoint(p_transport, p_address));
}

bool NetwConnectHandle::endpoint_add(
    const Variant &p_transport,
    const String &p_address,
    const String &p_display_name
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return false;
    }
    const RID transport_rid = resolve_transport(p_transport);
    if (!transport_rid.is_valid()) {
        return false;
    }
    const RID target
        = api->endpoint_add(transport_rid, p_address, p_display_name);
    if (target.is_valid()) {
        remember_endpoint(target);
    }
    return target.is_valid();
}

void NetwConnectHandle::endpoint_remove(
    const Variant &p_transport,
    const String &p_address
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    const RID target = resolve_endpoint(p_transport, p_address);
    if (target.is_valid()) {
        api->endpoint_remove(target);
    }
}

Error NetwConnectHandle::endpoint_set_display_name(
    const Variant &p_transport,
    const String &p_address,
    const String &p_display_name
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    const RID target = resolve_endpoint(p_transport, p_address);
    if (!target.is_valid()) {
        return ERR_DOES_NOT_EXIST;
    }
    return api->endpoint_set_param(
        target,
        NetwMultiplayer::ENDPOINT_PARAM_DISPLAY_NAME,
        p_display_name
    );
}

void NetwConnectHandle::endpoint_probe(
    const Variant &p_transport,
    const String &p_address
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    const RID target = resolve_endpoint(p_transport, p_address);
    if (target.is_valid()) {
        api->endpoint_probe(target);
    }
}

void NetwConnectHandle::endpoint_refresh() {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->endpoint_refresh();
    }
}

void NetwConnectHandle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD(
            "create_peer",
            "transport",
            "mode",
            "address",
            "settings",
            "completed",
            "progress"
        ),
        &NetwConnectHandle::create_peer,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("cancel_peer_creation", "ticket"),
        &NetwConnectHandle::cancel_peer_creation
    );

    ClassDB::bind_method(
        D_METHOD("get_join_address"),
        &NetwConnectHandle::get_join_address
    );
    ClassDB::bind_method(
        D_METHOD("diagnostics", "peer_id"),
        &NetwConnectHandle::diagnostics
    );
    ClassDB::bind_method(
        D_METHOD("transports"),
        &NetwConnectHandle::transports
    );
    ClassDB::bind_method(
        D_METHOD("transport", "of"),
        &NetwConnectHandle::transport
    );
    ClassDB::bind_method(
        D_METHOD("register_transport", "type"),
        &NetwConnectHandle::register_transport
    );
    ClassDB::bind_method(
        D_METHOD("unregister_transport", "of"),
        &NetwConnectHandle::unregister_transport
    );
    ClassDB::bind_method(
        D_METHOD("join_schema"),
        &NetwConnectHandle::join_schema
    );

    ClassDB::bind_method(D_METHOD("endpoints"), &NetwConnectHandle::endpoints);
    ClassDB::bind_method(
        D_METHOD("endpoint", "transport", "address"),
        &NetwConnectHandle::endpoint
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_add", "transport", "address", "display_name"),
        &NetwConnectHandle::endpoint_add,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_remove", "transport", "address"),
        &NetwConnectHandle::endpoint_remove
    );
    ClassDB::bind_method(
        D_METHOD(
            "endpoint_set_display_name",
            "transport",
            "address",
            "display_name"
        ),
        &NetwConnectHandle::endpoint_set_display_name
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_probe", "transport", "address"),
        &NetwConnectHandle::endpoint_probe
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_refresh"),
        &NetwConnectHandle::endpoint_refresh
    );

    ADD_SIGNAL(MethodInfo(
        SIG_JOIN_FAILED,
        PropertyInfo(Variant::INT, "error"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ENDPOINT_ADDED,
        PropertyInfo(Variant::STRING_NAME, "peer_class"),
        PropertyInfo(Variant::STRING, "address")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ENDPOINT_REMOVED,
        PropertyInfo(Variant::STRING_NAME, "peer_class"),
        PropertyInfo(Variant::STRING, "address")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ENDPOINT_UPDATED,
        PropertyInfo(Variant::STRING_NAME, "peer_class"),
        PropertyInfo(Variant::STRING, "address")
    ));

    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "join_address"),
        "",
        "get_join_address"
    );
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "endpoints"), "", "endpoints");
}

} // namespace netw
