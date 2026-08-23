#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

// TODO: unregister this class and delete this header once the spawn pump is
// native and calls netw::repl::reconcile over its own rows.
class NetwSpawnPlanner : public godot::RefCounted {
    GDCLASS(NetwSpawnPlanner, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static godot::PackedInt64Array ancestry_order(
        const godot::PackedInt64Array &p_routes,
        const godot::Dictionary &p_parents
    );

    static godot::Array reconcile(
        const godot::Array &p_rows,
        const godot::PackedInt32Array &p_peers
    );
};

} // namespace netw
