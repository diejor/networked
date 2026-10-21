#include "netw/api/connection.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/core.hpp"
#include "netw/connect/transport.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_ENDPOINT_ADDED = "endpoint_added";
const char *SIG_ENDPOINT_REMOVED = "endpoint_removed";
const char *SIG_ENDPOINT_CHANGED = "changed";
const char *SIG_STATE_CHANGED = "state_changed";
const char *SIG_PROGRESSED = "progressed";
const char *SIG_FAILED = "failed";
const char *SIG_JOIN_ADDRESS_CHANGED = "join_address_changed";
const char *SIG_INVITED = "invited";

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

void NetwConnection::announce_state_changed(int64_t p_state) {
    if (p_state != STATE_ONLINE) {
        current_endpoint.unref();
    }
    emit_signal(StringName(SIG_STATE_CHANGED), p_state);
}

void NetwConnection::announce_progressed(
    const String &p_message,
    double p_ratio
) {
    emit_signal(StringName(SIG_PROGRESSED), p_message, p_ratio);
}

bool NetwConnection::announce_failed(int64_t p_error, const String &p_reason) {
    const bool heard = has_connections(StringName(SIG_FAILED));
    emit_signal(StringName(SIG_FAILED), p_error, p_reason);
    return heard;
}

void NetwConnection::announce_join_address_changed(const String &p_address) {
    emit_signal(StringName(SIG_JOIN_ADDRESS_CHANGED), p_address);
}

Ref<NetwEndpoint> NetwConnection::remember_endpoint(const RID &p_target) {
    const HashMap<RID, Ref<NetwEndpoint>>::ConstIterator found
        = endpoints_by_row.find(p_target);
    if (found != endpoints_by_row.end()) {
        return found->value;
    }
    NetwMultiplayer *api = session();
    if (api == nullptr || !p_target.is_valid()) {
        return Ref<NetwEndpoint>();
    }
    const RID transport_rid = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_TRANSPORT
    );
    const StringName peer_class = api->transport_class_of(transport_rid);
    if (peer_class.is_empty()) {
        return Ref<NetwEndpoint>();
    }
    const String address = api->endpoint_get_param(
        p_target,
        NetwMultiplayer::ENDPOINT_PARAM_ADDRESS
    );
    Ref<NetwEndpoint> made;
    made.instantiate();
    made->bind_to(api, p_target, peer_class, address);
    endpoints_by_row.insert(p_target, made);
    return made;
}

void NetwConnection::announce_endpoint_added(const RID &p_target) {
    const Ref<NetwEndpoint> held = remember_endpoint(p_target);
    if (held.is_valid()) {
        emit_signal(StringName(SIG_ENDPOINT_ADDED), held);
    }
}

void NetwConnection::announce_endpoint_removed(const RID &p_target) {
    const HashMap<RID, Ref<NetwEndpoint>>::ConstIterator found
        = endpoints_by_row.find(p_target);
    if (found == endpoints_by_row.end()) {
        return;
    }
    const Ref<NetwEndpoint> held = found->value;
    endpoints_by_row.erase(p_target);
    held->unlist();
    emit_signal(StringName(SIG_ENDPOINT_REMOVED), held);
}

void NetwConnection::announce_invited(const RID &p_target) {
    const Ref<NetwEndpoint> held = remember_endpoint(p_target);
    if (held.is_valid()) {
        emit_signal(StringName(SIG_INVITED), held);
    }
}

void NetwConnection::announce_endpoint_updated(const RID &p_target) {
    const Ref<NetwEndpoint> held = remember_endpoint(p_target);
    if (held.is_valid()) {
        held->emit_signal(StringName(SIG_ENDPOINT_CHANGED));
    }
}

RID NetwConnection::create_peer(
    const Variant &p_transport,
    NetwConnection::TransportMode p_mode,
    const String &p_address,
    const Dictionary &p_settings,
    const Callable &p_completed
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
        p_completed
    );
}

void NetwConnection::cancel_peer_creation(const RID &p_ticket) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->transport_cancel_peer_creation(p_ticket);
    }
}

NetwConnection::State NetwConnection::get_state() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? State(api->transport_get_attempt_state())
                          : STATE_OFFLINE;
}

Ref<NetwPromise> NetwConnection::join(
    const Ref<NetwEndpoint> &p_endpoint,
    const Dictionary &p_settings
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwPromise::rejected(ERR_UNCONFIGURED, "The session closed.");
    }
    if (p_endpoint.is_null()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "NetwConnection.join needs an endpoint."
        );
    }
    return api->endpoint_join(p_endpoint->get_row(), p_settings);
}

Ref<NetwPromise> NetwConnection::host(
    const Variant &p_transport,
    const Dictionary &p_settings
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwPromise::rejected(ERR_UNCONFIGURED, "The session closed.");
    }
    return api->transport_host(resolve_transport(p_transport), p_settings);
}

void NetwConnection::cancel() {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->transport_cancel_attempt();
    }
}

Error NetwConnection::set_defaults(
    const Variant &p_transport,
    const Dictionary &p_settings
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    return api->transport_set_defaults(
        resolve_transport(p_transport),
        p_settings
    );
}

Dictionary NetwConnection::defaults(const Variant &p_transport) const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return Dictionary();
    }
    return api->transport_get_defaults(resolve_transport(p_transport));
}

String NetwConnection::get_join_address() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_join_address() : String();
}

void NetwConnection::set_join_address(const String &p_address) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->connection_set_join_address(p_address);
    }
}

int64_t NetwConnection::get_max_players() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->connection_max_players() : 0;
}

Ref<NetwEndpoint> NetwConnection::get_current_endpoint() {
    NetwMultiplayer *api = session();
    const connect::Transport *live
        = api != nullptr ? api->connect_plane().live_transport() : nullptr;
    const String address
        = api != nullptr ? api->connect_plane().endpoint_address() : String();
    if (live == nullptr || address.is_empty()) {
        current_endpoint.unref();
        return current_endpoint;
    }
    const StringName peer_class = live->peer_class();
    if (current_endpoint.is_valid()
        && current_endpoint->get_peer_class() == peer_class
        && current_endpoint->get_address() == address) {
        return current_endpoint;
    }
    current_endpoint = find(peer_class, address);
    if (current_endpoint.is_null()) {
        current_endpoint.instantiate();
        current_endpoint->bind_to(api, RID(), peer_class, address);
    }
    return current_endpoint;
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

TypedArray<NetwEndpoint> NetwConnection::endpoints() {
    NetwMultiplayer *api = session();
    TypedArray<NetwEndpoint> listed;
    if (api == nullptr) {
        return listed;
    }
    for (const Variant &entry : api->endpoint_list()) {
        const Ref<NetwEndpoint> held = remember_endpoint(entry);
        if (held.is_valid()) {
            listed.push_back(held);
        }
    }
    return listed;
}

Ref<NetwEndpoint> NetwConnection::add(
    const Variant &p_transport,
    const String &p_address,
    const String &p_display_name
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return Ref<NetwEndpoint>();
    }
    const RID transport_rid = resolve_transport(p_transport);
    if (!transport_rid.is_valid()) {
        return Ref<NetwEndpoint>();
    }
    return remember_endpoint(
        api->endpoint_add(transport_rid, p_address, p_display_name)
    );
}

Ref<NetwEndpoint> NetwConnection::invite(
    const Variant &p_transport,
    const String &p_address,
    const String &p_from_name
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return Ref<NetwEndpoint>();
    }
    return remember_endpoint(api->connect_plane().invite(
        resolve_transport(p_transport), p_address, p_from_name
    ));
}

Ref<NetwEndpoint> NetwConnection::find(
    const Variant &p_transport,
    const String &p_address
) {
    return remember_endpoint(resolve_endpoint(p_transport, p_address));
}

Error NetwConnection::remove(const Ref<NetwEndpoint> &p_endpoint) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    if (p_endpoint.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const RID target = p_endpoint->get_row();
    const HashMap<RID, Ref<NetwEndpoint>>::ConstIterator found
        = endpoints_by_row.find(target);
    if (found == endpoints_by_row.end()
        || found->value.ptr() != p_endpoint.ptr()) {
        return ERR_DOES_NOT_EXIST;
    }
    const int64_t flags = api->endpoint_get_state(
        target,
        NetwMultiplayer::ENDPOINT_STATE_FLAGS
    );
    if ((flags & NetwMultiplayer::ENDPOINT_FLAG_CALLER) == 0) {
        return ERR_INVALID_PARAMETER;
    }
    api->endpoint_remove(target);
    return OK;
}

void NetwConnection::set_browsing(bool p_browsing) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->endpoint_set_browsing(p_browsing);
    }
}

bool NetwConnection::is_browsing() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->endpoint_is_browsing();
}

void NetwConnection::set_probe_interval(double p_seconds) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->endpoint_set_probe_interval(p_seconds);
    }
}

double NetwConnection::get_probe_interval() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->endpoint_get_probe_interval() : 10.0;
}

void NetwConnection::_bind_methods() {
    BIND_ENUM_CONSTANT(STATE_OFFLINE);
    BIND_ENUM_CONSTANT(STATE_CREATING);
    BIND_ENUM_CONSTANT(STATE_CONNECTING);
    BIND_ENUM_CONSTANT(STATE_ONLINE);
    BIND_ENUM_CONSTANT(TRANSPORT_MODE_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_MODE_CLIENT);
    BIND_ENUM_CONSTANT(TRANSPORT_AVAILABLE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_PROBE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_BROWSE);
    BIND_ENUM_CONSTANT(TRANSPORT_ACCEPTS_EMPTY_ADDRESS);
    ClassDB::bind_method(
        D_METHOD(
            "create_peer",
            "transport",
            "mode",
            "address",
            "settings",
            "completed"
        ),
        &NetwConnection::create_peer
    );
    ClassDB::bind_method(
        D_METHOD("cancel_peer_creation", "ticket"),
        &NetwConnection::cancel_peer_creation
    );
    ClassDB::bind_method(D_METHOD("get_state"), &NetwConnection::get_state);
    ClassDB::bind_method(
        D_METHOD("join", "endpoint", "settings"),
        &NetwConnection::join,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("host", "transport", "settings"),
        &NetwConnection::host,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(D_METHOD("cancel"), &NetwConnection::cancel);
    ClassDB::bind_method(
        D_METHOD("set_defaults", "transport", "settings"),
        &NetwConnection::set_defaults
    );
    ClassDB::bind_method(
        D_METHOD("defaults", "transport"),
        &NetwConnection::defaults
    );

    ClassDB::bind_method(
        D_METHOD("get_join_address"),
        &NetwConnection::get_join_address
    );
    ClassDB::bind_method(
        D_METHOD("set_join_address", "address"),
        &NetwConnection::set_join_address
    );
    ClassDB::bind_method(
        D_METHOD("get_max_players"),
        &NetwConnection::get_max_players
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
        D_METHOD("add", "transport", "address", "display_name"),
        &NetwConnection::add,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("invite", "transport", "address", "from_name"),
        &NetwConnection::invite,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("find", "transport", "address"),
        &NetwConnection::find
    );
    ClassDB::bind_method(
        D_METHOD("remove", "endpoint"),
        &NetwConnection::remove
    );
    ClassDB::bind_method(
        D_METHOD("set_browsing", "browsing"),
        &NetwConnection::set_browsing
    );
    ClassDB::bind_method(D_METHOD("is_browsing"), &NetwConnection::is_browsing);
    ClassDB::bind_method(
        D_METHOD("set_probe_interval", "seconds"),
        &NetwConnection::set_probe_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_probe_interval"),
        &NetwConnection::get_probe_interval
    );

    ADD_SIGNAL(MethodInfo(
        SIG_STATE_CHANGED,
        PropertyInfo(
            Variant::INT,
            "state",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwConnection.State"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PROGRESSED,
        PropertyInfo(Variant::STRING, "message"),
        PropertyInfo(Variant::FLOAT, "ratio")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_FAILED,
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
        SIG_JOIN_ADDRESS_CHANGED,
        PropertyInfo(Variant::STRING, "address")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_INVITED,
        PropertyInfo(
            Variant::OBJECT,
            "endpoint",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEndpoint"
        )
    ));
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "state",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwConnection.State"
        ),
        "",
        "get_state"
    );
    ADD_SIGNAL(MethodInfo(
        SIG_ENDPOINT_ADDED,
        PropertyInfo(
            Variant::OBJECT,
            "endpoint",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEndpoint"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ENDPOINT_REMOVED,
        PropertyInfo(
            Variant::OBJECT,
            "endpoint",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEndpoint"
        )
    ));

    ClassDB::bind_method(
        D_METHOD("get_current_endpoint"), &NetwConnection::get_current_endpoint
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "current_endpoint",
            PROPERTY_HINT_RESOURCE_TYPE, "NetwEndpoint"),
        "",
        "get_current_endpoint"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "join_address"),
        "set_join_address",
        "get_join_address"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_players"),
        "",
        "get_max_players"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "endpoints",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEndpoint"
        ),
        "",
        "get_endpoints"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "browsing"),
        "set_browsing",
        "is_browsing"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "probe_interval",
            PROPERTY_HINT_RANGE,
            "0,60,0.1,or_greater,suffix:s"
        ),
        "set_probe_interval",
        "get_probe_interval"
    );
}

} // namespace netw
