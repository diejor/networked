#include "netw/action_gate_book.hpp"

#include "godot/object.hpp"
#include "godot/spatial_node.hpp"

namespace netw {

using namespace godot;

bool ActionGateBook::write_visible(Gate &r_gate, bool p_value) {
    Node *owner = Object::cast_to<Node>(gd::object_of(r_gate.owner));
    if (owner == nullptr) {
        return false;
    }
    if (CanvasItem *item = Object::cast_to<CanvasItem>(owner)) {
        if (!r_gate.hidden) {
            r_gate.original_visible = item->is_visible();
        }
        item->set_visible(p_value);
        return true;
    }
    if (Node3D *spatial = Object::cast_to<Node3D>(owner)) {
        if (!r_gate.hidden) {
            r_gate.original_visible = spatial->is_visible();
        }
        spatial->set_visible(p_value);
        return true;
    }
    return false;
}

int ActionGateBook::index_of(int64_t p_route) const {
    for (uint32_t at = 0; at < gates.size(); ++at) {
        if (gates[at].route == p_route) {
            return int(at);
        }
    }
    return -1;
}

bool ActionGateBook::arm(
    int64_t p_route,
    Node *p_owner,
    int64_t p_action_tick,
    int64_t p_display_tick
) {
    if (index_of(p_route) >= 0 || p_owner == nullptr) {
        return false;
    }
    if (p_action_tick < 0 || p_display_tick >= p_action_tick) {
        return false;
    }
    Gate gate;
    gate.route = p_route;
    gate.owner = p_owner->get_instance_id();
    gate.action_tick = p_action_tick;
    if (!write_visible(gate, false)) {
        return false;
    }
    gate.hidden = true;
    gates.push_back(gate);
    return true;
}

PackedInt64Array ActionGateBook::reveal_reached(int64_t p_display_tick) {
    PackedInt64Array out;
    for (uint32_t at = 0; at < gates.size();) {
        if (p_display_tick < gates[at].action_tick) {
            ++at;
            continue;
        }
        const int64_t route = gates[at].route;
        if (gates[at].hidden) {
            write_visible(gates[at], gates[at].original_visible);
            gates[at].hidden = false;
        }
        gates.remove_at(at);
        out.push_back(route);
    }
    return out;
}

bool ActionGateBook::reveal(int64_t p_route) {
    const int at = index_of(p_route);
    if (at < 0) {
        return false;
    }
    if (gates[at].hidden) {
        write_visible(gates[at], gates[at].original_visible);
        gates[at].hidden = false;
    }
    gates.remove_at(uint32_t(at));
    return true;
}

void ActionGateBook::drop(int64_t p_route) {
    const int at = index_of(p_route);
    if (at >= 0) {
        gates.remove_at(uint32_t(at));
    }
}

void ActionGateBook::clear() {
    gates.clear();
}

bool ActionGateBook::holds(int64_t p_route) const {
    return index_of(p_route) >= 0;
}

} // namespace netw
