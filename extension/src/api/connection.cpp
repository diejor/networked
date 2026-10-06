#include "netw/api/connection.hpp"

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

NetwMultiplayer *NetwConnection::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwConnection::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session == nullptr) {
        return;
    }
    for (const RID &target : p_session->endpoint_list()) {
        remember_endpoint(target);
    }
}

void NetwConnection::announce_join_failed(
    int64_t p_error,
    const String &p_reason
) {
    emit_signal(StringName(SIG_JOIN_FAILED), p_error, p_reason);
}

void NetwConnection::remember_endpoint(const RID &p_target) {
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

void NetwConnection::announce_endpoint_added(const RID &p_target) {
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

void NetwConnection::announce_endpoint_removed(const RID &p_target) {
    const HashMap<RID, EndpointKey>::ConstIterator found
        = endpoint_keys.find(p_target);
    if (found == endpoint_keys.end()) {
        return;
    }
    const EndpointKey key = found->value;
    endpoint_keys.erase(p_target);
    emit_signal(StringName(SIG_ENDPOINT_REMOVED), key.peer_class, key.address);
}

void NetwConnection::announce_endpoint_updated(const RID &p_target) {
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

RID NetwConnection::create_peer(
    const Variant &p_transport,
    NetwConnection::TransportMode p_mode,
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

void NetwConnection::cancel_peer_creation(const RID &p_ticket) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->transport_cancel_peer_creation(p_ticket);
    }
}

String NetwConnection::get_join_address() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_join_address() : String();
}

Dictionary NetwConnection::diagnostics(int64_t p_peer_id) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_diagnostics(p_peer_id) : Dictionary();
}

RID NetwConnection::resolve_transport(const Variant &p_of) const {
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

RID NetwConnection::resolve_endpoint(
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

Dictionary NetwConnection::transport_snapshot(const RID &p_transport) const {
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
    snapshot["timeout_hint"] = api->transport_get_param(
        p_transport,
        NetwMultiplayer::TRANSPORT_PARAM_TIMEOUT_HINT
    );
    return snapshot;
}

Dictionary NetwConnection::endpoint_snapshot(const RID &p_target) const {
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
        NetwConnection::ENDPOINT_STATE_FLAGS
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
        NetwConnection::ENDPOINT_STATE_STATUS
    );
    snapshot["status"]
        = status.get_type() == Variant::NIL ? Variant(int64_t(FAILED)) : status;
    snapshot["info"] = api->endpoint_get_state(
        p_target,
        NetwConnection::ENDPOINT_STATE_INFO
    );
    snapshot["is_caller_added"]
        = (flags & NetwConnection::ENDPOINT_FLAG_CALLER) != 0;
    snapshot["is_available"]
        = (flags & NetwConnection::ENDPOINT_FLAG_AVAILABLE) != 0;
    snapshot["is_observed"]
        = (flags & NetwConnection::ENDPOINT_FLAG_OBSERVED) != 0;
    return snapshot;
}

Array NetwConnection::transports() const {
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

Dictionary NetwConnection::transport(const Variant &p_of) const {
    return transport_snapshot(resolve_transport(p_of));
}

bool NetwConnection::register_transport(const Ref<Script> &p_type) {
    NetwMultiplayer *api = session();
    return api != nullptr && api->transport_register(p_type).is_valid();
}

bool NetwConnection::unregister_transport(const Variant &p_of) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return false;
    }
    return api->transport_unregister(resolve_transport(p_of)) == OK;
}

Array NetwConnection::join_schema() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_join_schema() : Array();
}

Array NetwConnection::endpoints() const {
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

Dictionary NetwConnection::endpoint(
    const Variant &p_transport,
    const String &p_address
) const {
    return endpoint_snapshot(resolve_endpoint(p_transport, p_address));
}

bool NetwConnection::endpoint_add(
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

void NetwConnection::endpoint_remove(
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

Error NetwConnection::endpoint_set_display_name(
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

void NetwConnection::endpoint_probe(
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

Error NetwConnection::transport_set_browse_settings(
    const Variant &p_transport,
    const Dictionary &p_settings
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    return api->transport_set_browse_settings(
        resolve_transport(p_transport),
        p_settings
    );
}

void NetwConnection::endpoint_refresh() {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->endpoint_refresh();
    }
}

void NetwConnection::_bind_methods() {
    BIND_ENUM_CONSTANT(TRANSPORT_MODE_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_MODE_CLIENT);
    BIND_ENUM_CONSTANT(TRANSPORT_AVAILABLE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_PROBE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_BROWSE);
    BIND_ENUM_CONSTANT(TRANSPORT_ACCEPTS_EMPTY_ADDRESS);
    BIND_ENUM_CONSTANT(ENDPOINT_STATE_FLAGS);
    BIND_ENUM_CONSTANT(ENDPOINT_STATE_STATUS);
    BIND_ENUM_CONSTANT(ENDPOINT_STATE_INFO);
    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_CALLER);
    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_AVAILABLE);
    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_OBSERVED);
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
        &NetwConnection::create_peer,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("cancel_peer_creation", "ticket"),
        &NetwConnection::cancel_peer_creation
    );

    ClassDB::bind_method(
        D_METHOD("get_join_address"),
        &NetwConnection::get_join_address
    );
    ClassDB::bind_method(
        D_METHOD("diagnostics", "peer_id"),
        &NetwConnection::diagnostics
    );
    ClassDB::bind_method(D_METHOD("transports"), &NetwConnection::transports);
    ClassDB::bind_method(
        D_METHOD("transport", "of"),
        &NetwConnection::transport
    );
    ClassDB::bind_method(
        D_METHOD("register_transport", "type"),
        &NetwConnection::register_transport
    );
    ClassDB::bind_method(
        D_METHOD("unregister_transport", "of"),
        &NetwConnection::unregister_transport
    );
    ClassDB::bind_method(D_METHOD("join_schema"), &NetwConnection::join_schema);

    ClassDB::bind_method(D_METHOD("get_endpoints"), &NetwConnection::endpoints);
    ClassDB::bind_method(
        D_METHOD("endpoint", "transport", "address"),
        &NetwConnection::endpoint
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_add", "transport", "address", "display_name"),
        &NetwConnection::endpoint_add,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_remove", "transport", "address"),
        &NetwConnection::endpoint_remove
    );
    ClassDB::bind_method(
        D_METHOD(
            "endpoint_set_display_name",
            "transport",
            "address",
            "display_name"
        ),
        &NetwConnection::endpoint_set_display_name
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_probe", "transport", "address"),
        &NetwConnection::endpoint_probe
    );
    ClassDB::bind_method(
        D_METHOD("transport_set_browse_settings", "transport", "settings"),
        &NetwConnection::transport_set_browse_settings
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_refresh"),
        &NetwConnection::endpoint_refresh
    );

    ADD_SIGNAL(MethodInfo(
        SIG_JOIN_FAILED,
        PropertyInfo(
            Variant::INT,
            "error",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "Error"
        ),
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
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "endpoints"),
        "",
        "get_endpoints"
    );
}

} // namespace netw
