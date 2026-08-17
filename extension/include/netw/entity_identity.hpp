#pragma once

/* An entity's identity, and the node name that spells two of its four parts.
 *
 * A spawned entity arrives at a peer as a node, so the pair that says what it
 * is and whom it represents has to survive a round trip through the node's
 * name: `entity_id|peer_id`, with a zero peer meaning the entity represents no
 * player. That makes the separator part of the identity's domain rather than a
 * formatting detail, because an entity id holding one produces a name that
 * parses back to no entity at all. `format` refuses such an id instead of
 * answering with that name.
 *
 * `route` and `rid` are the same identity as the session addresses it. Neither
 * is spelled in the name: the RID exists from creation and is local to the
 * process that holds it, and the route is minted when the entity is bound to
 * the wire.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

struct EntityIdentity {
    godot::StringName entity_id;
    int64_t peer_id = 0;
    int64_t route = 0;
    godot::RID rid;

    // Empty when `p_entity_id` cannot be named, which is the refusal.
    static godot::String format(
        const godot::String &p_entity_id,
        int64_t p_peer_id
    );

    static godot::StringName parse_entity(const godot::String &p_node_name);

    static int64_t parse_peer(const godot::String &p_node_name);
};

// The identity codec as GDScript reaches it.
class NetwEntityIdentity : public godot::RefCounted {
    GDCLASS(NetwEntityIdentity, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static godot::String format_name(
        const godot::String &p_entity_id,
        int64_t p_peer_id
    );
    static godot::StringName parse_entity(const godot::String &p_node_name);
    static int64_t parse_peer(const godot::String &p_node_name);
};

} // namespace netw
