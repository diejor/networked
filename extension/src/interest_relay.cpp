#include "netw/interest_relay.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

bool is_kind(int64_t value) {
    return value == NetwInterestAwareness::ENTER
        || value == NetwInterestAwareness::EXIT;
}

} // namespace

Ref<NetwInterestAwareness> NetwInterestAwareness::layer_edge(
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
    return from_array(raw);
}

Ref<NetwInterestAwareness> NetwInterestAwareness::observer_edge(
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
    return from_array(raw);
}

Ref<NetwInterestAwareness> NetwInterestAwareness::from_array(const Variant &raw
) {
    const Ref<NetwInterestAwareness> refused;
    if (raw.get_type() != Variant::ARRAY) {
        return refused;
    }
    const Array row = raw;
    if (row.size() != 5) {
        return refused;
    }
    if (row[0].get_type() != Variant::INT || row[1].get_type() != Variant::INT
        || row[2].get_type() != Variant::STRING_NAME
        || row[3].get_type() != Variant::INT
        || row[4].get_type() != Variant::INT) {
        return refused;
    }
    const int64_t row_type = row[0];
    const int64_t row_route = row[1];
    const StringName row_layer = row[2];
    const int64_t row_observer = row[3];
    const int64_t row_kind = row[4];
    if (row_type != LAYER && row_type != OBSERVER) {
        return refused;
    }
    if (row_route <= 0 || row_layer.is_empty() || row_observer < 0
        || !is_kind(row_kind)) {
        return refused;
    }
    if ((row_type == LAYER) != (row_observer == 0)) {
        return refused;
    }
    Ref<NetwInterestAwareness> out;
    out.instantiate();
    out->type = int32_t(row_type);
    out->route = row_route;
    out->layer_id = row_layer;
    out->observer_peer = row_observer;
    out->kind = int32_t(row_kind);
    return out;
}

Array NetwInterestAwareness::to_array() const {
    Array out;
    out.push_back(int64_t(type));
    out.push_back(route);
    out.push_back(layer_id);
    out.push_back(observer_peer);
    out.push_back(int64_t(kind));
    return out;
}

void NetwInterestAwareness::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwInterestAwareness",
        D_METHOD("layer_edge", "route", "layer_id", "kind"),
        &NetwInterestAwareness::layer_edge
    );
    ClassDB::bind_static_method(
        "NetwInterestAwareness",
        D_METHOD("observer_edge", "route", "layer_id", "observer_peer", "kind"),
        &NetwInterestAwareness::observer_edge
    );
    ClassDB::bind_static_method(
        "NetwInterestAwareness",
        D_METHOD("from_array", "raw"),
        &NetwInterestAwareness::from_array
    );
    ClassDB::bind_method(
        D_METHOD("to_array"),
        &NetwInterestAwareness::to_array
    );
    ClassDB::bind_method(
        D_METHOD("get_edge_type"),
        &NetwInterestAwareness::get_edge_type
    );
    ClassDB::bind_method(
        D_METHOD("get_route"),
        &NetwInterestAwareness::get_route
    );
    ClassDB::bind_method(
        D_METHOD("get_layer_id"),
        &NetwInterestAwareness::get_layer_id
    );
    ClassDB::bind_method(
        D_METHOD("get_observer_peer"),
        &NetwInterestAwareness::get_observer_peer
    );
    ClassDB::bind_method(
        D_METHOD("get_kind"),
        &NetwInterestAwareness::get_kind
    );

    BIND_ENUM_CONSTANT(LAYER);
    BIND_ENUM_CONSTANT(OBSERVER);
}

void NetwInterestRelay::append(
    int64_t peer_id,
    const Ref<NetwInterestAwareness> &edge
) {
    if (edge.is_null()) {
        return;
    }
    if (!pending.has(peer_id)) {
        pending.insert(peer_id, Array());
        peer_order.push_back(peer_id);
    }
    pending[peer_id].push_back(edge);
}

PackedInt64Array NetwInterestRelay::targets() const {
    PackedInt64Array out;
    for (uint32_t index = 0; index < peer_order.size(); ++index) {
        out.push_back(peer_order[index]);
    }
    return out;
}

Array NetwInterestRelay::wire_for(int64_t peer_id) const {
    Array out;
    const HashMap<int64_t, Array>::ConstIterator found = pending.find(peer_id);
    if (!found) {
        return out;
    }
    for (int index = 0; index < found->value.size(); ++index) {
        const Ref<NetwInterestAwareness> edge = found->value[index];
        out.push_back(edge->to_array());
    }
    return out;
}

void NetwInterestRelay::forget(int64_t peer_id) {
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

bool NetwInterestRelay::is_empty() const {
    return pending.is_empty();
}

void NetwInterestRelay::clear() {
    pending.clear();
    peer_order.clear();
}

void NetwInterestRelay::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("append", "peer_id", "edge"),
        &NetwInterestRelay::append
    );
    ClassDB::bind_method(D_METHOD("targets"), &NetwInterestRelay::targets);
    ClassDB::bind_method(
        D_METHOD("wire_for", "peer_id"),
        &NetwInterestRelay::wire_for
    );
    ClassDB::bind_method(
        D_METHOD("forget", "peer_id"),
        &NetwInterestRelay::forget
    );
    ClassDB::bind_method(D_METHOD("is_empty"), &NetwInterestRelay::is_empty);
    ClassDB::bind_method(D_METHOD("clear"), &NetwInterestRelay::clear);
}

} // namespace netw
