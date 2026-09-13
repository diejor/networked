#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/rid.hpp"

namespace netw {

class ActionGateBook {
public:
    struct Gate {
        int64_t route = 0;
        godot::ObjectID owner;
        bool hidden = false;
        bool original_visible = true;
        int64_t action_tick = -1;
    };

private:
    godot::LocalVector<Gate> gates;

    static bool write_visible(Gate &r_gate, bool p_value);

    int index_of(int64_t p_route) const;

public:
    bool arm(
        int64_t p_route,
        godot::Node *p_owner,
        int64_t p_action_tick,
        int64_t p_display_tick
    );

    godot::PackedInt64Array reveal_reached(int64_t p_display_tick);

    bool reveal(int64_t p_route);

    void drop(int64_t p_route);

    void clear();

    bool holds(int64_t p_route) const;

    uint32_t size() const {
        return gates.size();
    }
};

} // namespace netw
