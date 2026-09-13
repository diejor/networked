#pragma once

#include "godot/variant.hpp"

namespace netw::spawn {

class Planner {
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

} // namespace netw::spawn
