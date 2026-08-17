#pragma once

/* The shell's handle on the materialization planner.
 *
 * A binding that exists so the GDScript spawn pump can plan a tick before
 * `NetwMultiplayer` is native. It holds no logic and no state: the verb
 * translates rows in, calls `netw::repl::reconcile`, and translates the plan
 * back out.
 *
 * TODO: unregister this class and delete this header once the spawn pump is
 * native and calls `netw::repl::reconcile` over its own rows.
 */

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

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
