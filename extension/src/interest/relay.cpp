#include "netw/interest/relay.hpp"

using namespace godot;

namespace netw::interest {

namespace {

bool is_kind(int64_t value) {
    return value == Awareness::ENTER || value == Awareness::EXIT;
}

} // namespace

Awareness Awareness::layer_edge(
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
    Awareness edge;
    from_array(raw, edge);
    return edge;
}

Awareness Awareness::observer_edge(
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
    Awareness edge;
    from_array(raw, edge);
    return edge;
}

bool Awareness::from_array(const Variant &raw, Awareness &r_edge) {
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

Array Awareness::to_array() const {
    Array out;
    out.push_back(int64_t(type));
    out.push_back(route);
    out.push_back(layer_id);
    out.push_back(observer_peer);
    out.push_back(int64_t(kind));
    return out;
}

namespace {

constexpr int EDGE_COUNT_BYTES = 2;

bool edge_is_coherent(const AwarenessEdge &edge) {
    return edge.route > 0 && !edge.layer_id.is_empty()
        && (edge.observer_scoped == (edge.observer > 0));
}

AwarenessEdge edge_of(const Awareness &source) {
    AwarenessEdge out;
    out.observer_scoped = source.type == Awareness::OBSERVER;
    out.route = uint64_t(source.route);
    out.layer_id = source.layer_id;
    out.observer = uint64_t(source.observer_peer);
    out.entered = source.kind == Awareness::ENTER;
    return out;
}

Awareness awareness_of(const AwarenessEdge &edge) {
    Awareness out;
    out.type = edge.observer_scoped ? Awareness::OBSERVER : Awareness::LAYER;
    out.route = int64_t(edge.route);
    out.layer_id = edge.layer_id;
    out.observer_peer = int64_t(edge.observer);
    out.kind = edge.entered ? Awareness::ENTER : Awareness::EXIT;
    return out;
}

} // namespace

PackedByteArray awareness_encode(const Array &edges) {
    wire::WriteStream stream;
    uint64_t count = uint64_t(edges.size());
    if (!stream.varuint(count, EDGE_COUNT_BYTES)) {
        return PackedByteArray();
    }
    for (int at = 0; at < edges.size(); ++at) {
        Awareness source;
        if (!Awareness::from_array(edges[at], source)) {
            return PackedByteArray();
        }
        AwarenessEdge edge = edge_of(source);
        if (!AwarenessEdge::wire.run(stream, edge)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool awareness_decode(
    const PackedByteArray &bytes,
    LocalVector<Awareness> &r_edges
) {
    wire::ReadStream stream(bytes);
    uint64_t count = 0;
    if (!stream.varuint(count, EDGE_COUNT_BYTES)) {
        return false;
    }
    LocalVector<Awareness> staged;
    staged.reserve(uint32_t(count));
    for (uint64_t at = 0; at < count; ++at) {
        AwarenessEdge edge;
        if (!AwarenessEdge::wire.run(stream, edge) || !edge_is_coherent(edge)) {
            return false;
        }
        staged.push_back(awareness_of(edge));
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        return false;
    }
    r_edges = staged;
    return true;
}

Dictionary awareness_spec_records() {
    Dictionary out;
    out["AwarenessEdge"] = AwarenessEdge::wire.spec_dump();
    return out;
}

void Relay::append(int64_t peer_id, const Awareness &edge) {
    if (edge.route <= 0) {
        return;
    }
    if (!pending.has(peer_id)) {
        pending.insert(peer_id, Array());
        peer_order.push_back(peer_id);
    }
    pending[peer_id].push_back(edge.to_array());
}

PackedInt64Array Relay::targets() const {
    PackedInt64Array out;
    for (uint32_t index = 0; index < peer_order.size(); ++index) {
        out.push_back(peer_order[index]);
    }
    return out;
}

Array Relay::wire_for(int64_t peer_id) const {
    Array out;
    const HashMap<int64_t, Array>::ConstIterator found = pending.find(peer_id);
    if (!found) {
        return out;
    }
    out.append_array(found->value);
    return out;
}

void Relay::forget(int64_t peer_id) {
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

bool Relay::is_empty() const {
    return pending.is_empty();
}

void Relay::clear() {
    pending.clear();
    peer_order.clear();
}

} // namespace netw::interest
