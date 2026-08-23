#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

struct EntityIdentity {
    godot::StringName entity_id;
    int64_t peer_id = 0;
    int64_t route = 0;
    godot::RID rid;

    static godot::String format(
        const godot::String &p_entity_id,
        int64_t p_peer_id
    );

    static godot::StringName parse_entity(const godot::String &p_node_name);

    static int64_t parse_peer(const godot::String &p_node_name);
};

} // namespace netw
