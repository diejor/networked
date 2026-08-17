#pragma once

/* A wire reference to a node inside an entity: what a Node becomes when it
 * crosses as an argument, a spawn argument, or a request return value.
 *
 * A Node cannot be serialized, and a peer's copy of an entity is a different
 * instance anyway, so a node argument travels as its route plus the component
 * address that locates it under the entity root. The receiver rebinds it to
 * its own instance.
 *
 * It is a distinct kind on the wire rather than a sentinel dictionary, so it
 * can never collide with a value a game actually sent.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwNodeRef : public godot::RefCounted {
    GDCLASS(NetwNodeRef, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // The entity route the referenced node belongs to.
    int64_t route = 0;

    // 0 is the entity root, 1..254 index the registered component table, and
    // 255 means read `path`.
    int64_t comp = 0;

    godot::String path;

    static godot::Ref<NetwNodeRef> create(
        int64_t p_route,
        int64_t p_comp,
        const godot::String &p_path
    );

    int64_t get_route() const { return route; }
    void set_route(int64_t p_route) { route = p_route; }
    int64_t get_comp() const { return comp; }
    void set_comp(int64_t p_comp) { comp = p_comp; }
    godot::String get_path() const { return path; }
    void set_path(const godot::String &p_path) { path = p_path; }
};

} // namespace netw
