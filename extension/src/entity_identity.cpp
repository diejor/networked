#include "netw/entity_identity.hpp"

#include "netw/log.hpp"

namespace netw {

using namespace godot;

namespace {

const char *SEPARATOR = "|";

PackedStringArray exactly_two_identity_parts(const String &p_node_name) {
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
    const PackedStringArray parts = exactly_two_identity_parts(p_node_name);
    if (parts.size() != 2 || parts[0].is_empty()) {
        return StringName();
    }
    return StringName(parts[0]);
}

int64_t EntityIdentity::parse_peer(const String &p_node_name) {
    const PackedStringArray parts = exactly_two_identity_parts(p_node_name);
    if (parts.size() != 2) {
        return 0;
    }
    return parts[1].to_int();
}

} // namespace netw
