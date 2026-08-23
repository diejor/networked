#include "netw/interest_relay.hpp"

using namespace godot;

namespace netw {

namespace {

bool is_kind(int64_t value) {
    return value == InterestAwareness::ENTER
        || value == InterestAwareness::EXIT;
}

} // namespace

InterestAwareness InterestAwareness::layer_edge(
    int64_t route,
    const StringName &layer_id,
    int kind
) {
    Array raw;
    raw.push_back(int64_t(LAYER));
    raw.push_back(route);
    raw.push_back(layer_id);
    raw.push_back(int64_t(0));
    raw.push_back(int64_t(kind));
    InterestAwareness edge;
    from_array(raw, edge);
    return edge;
}

InterestAwareness InterestAwareness::observer_edge(
    int64_t route,
    const StringName &layer_id,
    int64_t observer_peer,
    int kind
) {
    Array raw;
    raw.push_back(int64_t(OBSERVER));
    raw.push_back(route);
    raw.push_back(layer_id);
    raw.push_back(observer_peer);
    raw.push_back(int64_t(kind));
    InterestAwareness edge;
    from_array(raw, edge);
    return edge;
}

bool InterestAwareness::from_array(
    const Variant &raw,
    InterestAwareness &r_edge
) {
    if (raw.get_type() != Variant::ARRAY) {
        return false;
    }
    const Array row = raw;
    if (row.size() != 5) {
        return false;
    }
    if (row[0].get_type() != Variant::INT || row[1].get_type() != Variant::INT
        || row[2].get_type() != Variant::STRING_NAME
        || row[3].get_type() != Variant::INT
        || row[4].get_type() != Variant::INT) {
        return false;
    }
    const int64_t row_type = row[0];
    const int64_t row_route = row[1];
    const StringName row_layer = row[2];
    const int64_t row_observer = row[3];
    const int64_t row_kind = row[4];
    if (row_type != LAYER && row_type != OBSERVER) {
        return false;
    }
    if (row_route <= 0 || row_layer.is_empty() || row_observer < 0
        || !is_kind(row_kind)) {
        return false;
    }
    if ((row_type == LAYER) != (row_observer == 0)) {
        return false;
    }
    r_edge.type = int32_t(row_type);
    r_edge.route = row_route;
    r_edge.layer_id = row_layer;
    r_edge.observer_peer = row_observer;
    r_edge.kind = int32_t(row_kind);
    return true;
}

Array InterestAwareness::to_array() const {
    Array out;
    out.push_back(int64_t(type));
    out.push_back(route);
    out.push_back(layer_id);
    out.push_back(observer_peer);
    out.push_back(int64_t(kind));
    return out;
}


void InterestRelay::append(
    int64_t peer_id,
    const InterestAwareness &edge
) {
    if (edge.route <= 0) {
        return;
    }
    if (!pending.has(peer_id)) {
        pending.insert(peer_id, Array());
        peer_order.push_back(peer_id);
    }
    pending[peer_id].push_back(edge.to_array());
}

PackedInt64Array InterestRelay::targets() const {
    PackedInt64Array out;
    for (uint32_t index = 0; index < peer_order.size(); ++index) {
        out.push_back(peer_order[index]);
    }
    return out;
}

Array InterestRelay::wire_for(int64_t peer_id) const {
    Array out;
    const HashMap<int64_t, Array>::ConstIterator found = pending.find(peer_id);
    if (!found) {
        return out;
    }
    out.append_array(found->value);
    return out;
}

void InterestRelay::forget(int64_t peer_id) {
    if (!pending.has(peer_id)) {
        return;
    }
    pending.erase(peer_id);
    for (uint32_t index = 0; index < peer_order.size(); ++index) {
        if (peer_order[index] == peer_id) {
            peer_order.remove_at(index);
            return;
        }
    }
}

bool InterestRelay::is_empty() const {
    return pending.is_empty();
}

void InterestRelay::clear() {
    pending.clear();
    peer_order.clear();
}

} // namespace netw
