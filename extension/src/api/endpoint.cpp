#include "netw/api/endpoint.hpp"

#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

enums::NetwEndpoint::Status endpoint_status(int64_t p_flags, int64_t p_error) {
    if ((p_flags & enums::NetwMultiplayer::ENDPOINT_FLAG_AVAILABLE) == 0) {
        return enums::NetwEndpoint::STATUS_UNSUPPORTED;
    }
    if ((p_flags & enums::NetwMultiplayer::ENDPOINT_FLAG_OBSERVED) == 0) {
        return enums::NetwEndpoint::STATUS_UNKNOWN;
    }
    switch (p_error) {
        case OK:
            return enums::NetwEndpoint::STATUS_ONLINE;
        case ERR_BUSY:
            return enums::NetwEndpoint::STATUS_FULL;
        case ERR_UNAUTHORIZED:
            return enums::NetwEndpoint::STATUS_INCOMPATIBLE;
        case ERR_UNAVAILABLE:
            return enums::NetwEndpoint::STATUS_UNKNOWN;
        default:
            return enums::NetwEndpoint::STATUS_UNREACHABLE;
    }
}

NetwMultiplayer *NetwEndpoint::listed_session() const {
    if (!listed) {
        return nullptr;
    }
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwEndpoint::bind_to(
    NetwMultiplayer *p_session,
    const RID &p_row,
    const StringName &p_peer_class,
    const String &p_address
) {
    session_id = gd::instance_id(p_session);
    row = p_row;
    peer_class = p_peer_class;
    address = p_address;
    listed = true;
}

void NetwEndpoint::unlist() {
    listed = false;
}

RID NetwEndpoint::get_row() const {
    return row;
}

StringName NetwEndpoint::get_peer_class() const {
    return peer_class;
}

String NetwEndpoint::get_address() const {
    return address;
}

String NetwEndpoint::get_display_name() const {
    NetwMultiplayer *api = listed_session();
    if (api == nullptr) {
        return String();
    }
    return api->endpoint_get_param(
        row,
        NetwMultiplayer::ENDPOINT_PARAM_DISPLAY_NAME
    );
}

String NetwEndpoint::get_source() const {
    NetwMultiplayer *api = listed_session();
    return api != nullptr ? api->endpoint_source(row) : String();
}

NetwEndpoint::Status NetwEndpoint::get_status() const {
    NetwMultiplayer *api = listed_session();
    if (api == nullptr) {
        return STATUS_UNKNOWN;
    }
    return endpoint_status(
        api->endpoint_get_state(row, NetwMultiplayer::ENDPOINT_STATE_FLAGS),
        api->endpoint_get_state(row, NetwMultiplayer::ENDPOINT_STATE_STATUS)
    );
}

Ref<NetwServerInfo> NetwEndpoint::get_info() {
    NetwMultiplayer *api = listed_session();
    if (api != nullptr) {
        const Ref<NetwServerInfo> answered = api->endpoint_get_state(
            row,
            NetwMultiplayer::ENDPOINT_STATE_INFO
        );
        if (answered.is_valid()) {
            return answered;
        }
    }
    if (unanswered.is_null()) {
        unanswered.instantiate();
    }
    return unanswered;
}

bool NetwEndpoint::get_is_listed() const {
    return listed_session() != nullptr;
}

void NetwEndpoint::probe() {
    NetwMultiplayer *api = listed_session();
    if (api != nullptr) {
        api->endpoint_probe(row);
    }
}

void NetwEndpoint::_bind_methods() {
    BIND_ENUM_CONSTANT(STATUS_UNKNOWN);
    BIND_ENUM_CONSTANT(STATUS_ONLINE);
    BIND_ENUM_CONSTANT(STATUS_FULL);
    BIND_ENUM_CONSTANT(STATUS_UNREACHABLE);
    BIND_ENUM_CONSTANT(STATUS_INCOMPATIBLE);
    BIND_ENUM_CONSTANT(STATUS_UNSUPPORTED);

    ClassDB::bind_method(
        D_METHOD("get_peer_class"),
        &NetwEndpoint::get_peer_class
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "peer_class"),
        "",
        "get_peer_class"
    );
    ClassDB::bind_method(D_METHOD("get_address"), &NetwEndpoint::get_address);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "address"), "", "get_address");
    ClassDB::bind_method(
        D_METHOD("get_display_name"),
        &NetwEndpoint::get_display_name
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "display_name"),
        "",
        "get_display_name"
    );
    ClassDB::bind_method(D_METHOD("get_source"), &NetwEndpoint::get_source);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "source"), "", "get_source");
    ClassDB::bind_method(D_METHOD("get_status"), &NetwEndpoint::get_status);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "status",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEndpoint.Status"
        ),
        "",
        "get_status"
    );
    ClassDB::bind_method(D_METHOD("get_info"), &NetwEndpoint::get_info);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "info",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwServerInfo",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_info"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_listed"),
        &NetwEndpoint::get_is_listed
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_listed"), "", "get_is_listed");
    ClassDB::bind_method(D_METHOD("probe"), &NetwEndpoint::probe);

    ADD_SIGNAL(MethodInfo("changed"));
}

} // namespace netw
