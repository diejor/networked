#include "netw/entity_identity.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

namespace netw {

using namespace godot;

namespace {

const char *SEPARATOR = "|";

// The two halves of a name that spells exactly one identity, or nothing.
PackedStringArray identity_parts(const String &p_node_name) {
    const PackedStringArray parts = p_node_name.split(SEPARATOR);
    if (parts.size() != 2) {
        return PackedStringArray();
    }
    return parts;
}

} // namespace

String EntityIdentity::format(const String &p_entity_id, int64_t p_peer_id) {
    NETW_ERR_COND_V(
        p_entity_id.find(SEPARATOR) >= 0,
        String(),
        sys::LIVENESS,
        "entity id %s cannot be named: it holds the identity separator",
        p_entity_id
    );
    return p_entity_id + String(SEPARATOR) + String::num_int64(p_peer_id);
}

StringName EntityIdentity::parse_entity(const String &p_node_name) {
    const PackedStringArray parts = identity_parts(p_node_name);
    if (parts.size() != 2 || parts[0].is_empty()) {
        return StringName();
    }
    return StringName(parts[0]);
}

int64_t EntityIdentity::parse_peer(const String &p_node_name) {
    const PackedStringArray parts = identity_parts(p_node_name);
    if (parts.size() != 2) {
        return 0;
    }
    return parts[1].to_int();
}

String NetwEntityIdentity::format_name(
    const String &p_entity_id,
    int64_t p_peer_id
) {
    return EntityIdentity::format(p_entity_id, p_peer_id);
}

StringName NetwEntityIdentity::parse_entity(const String &p_node_name) {
    return EntityIdentity::parse_entity(p_node_name);
}

int64_t NetwEntityIdentity::parse_peer(const String &p_node_name) {
    return EntityIdentity::parse_peer(p_node_name);
}

void NetwEntityIdentity::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwEntityIdentity",
        D_METHOD("format_name", "entity_id", "peer_id"),
        &NetwEntityIdentity::format_name
    );
    ClassDB::bind_static_method(
        "NetwEntityIdentity",
        D_METHOD("parse_entity", "node_name"),
        &NetwEntityIdentity::parse_entity
    );
    ClassDB::bind_static_method(
        "NetwEntityIdentity",
        D_METHOD("parse_peer", "node_name"),
        &NetwEntityIdentity::parse_peer
    );
}

} // namespace netw
