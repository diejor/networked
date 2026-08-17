#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwInterestAwareness : public godot::RefCounted {
    GDCLASS(NetwInterestAwareness, godot::RefCounted)

public:
    enum Kind {
        EXIT = 0,
        ENTER = 1,
    };

    enum Type {
        LAYER = 0,
        OBSERVER = 1,
    };

private:
    int32_t type = LAYER;
    int64_t route = 0;
    godot::StringName layer_id;
    int64_t observer_peer = 0;
    int32_t kind = ENTER;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwInterestAwareness> layer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int kind
    );
    static godot::Ref<NetwInterestAwareness> observer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int64_t observer_peer,
        int kind
    );

    static godot::Ref<NetwInterestAwareness> from_array(
        const godot::Variant &raw
    );
    godot::Array to_array() const;

    int get_edge_type() const { return type; }
    int64_t get_route() const { return route; }
    godot::StringName get_layer_id() const { return layer_id; }
    int64_t get_observer_peer() const { return observer_peer; }
    int get_kind() const { return kind; }
};

class NetwInterestRelay : public godot::RefCounted {
    GDCLASS(NetwInterestRelay, godot::RefCounted)

    godot::LocalVector<int64_t> peer_order;
    godot::HashMap<int64_t, godot::Array> pending;

protected:
    static void _bind_methods();

public:
    void append(int64_t peer_id, const godot::Ref<NetwInterestAwareness> &edge);

    godot::PackedInt64Array targets() const;

    godot::Array wire_for(int64_t peer_id) const;

    void forget(int64_t peer_id);
    bool is_empty() const;
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwInterestAwareness::Type);
