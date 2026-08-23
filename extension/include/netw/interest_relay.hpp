#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

struct InterestAwareness {
    enum Kind {
        EXIT = 0,
        ENTER = 1,
    };

    enum Type {
        LAYER = 0,
        OBSERVER = 1,
    };

    int32_t type = LAYER;
    int64_t route = 0;
    godot::StringName layer_id;
    int64_t observer_peer = 0;
    int32_t kind = ENTER;

    static InterestAwareness layer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int kind
    );
    static InterestAwareness observer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int64_t observer_peer,
        int kind
    );

    static bool from_array(
        const godot::Variant &raw,
        InterestAwareness &r_edge
    );
    godot::Array to_array() const;
};

class InterestRelay {
    godot::LocalVector<int64_t> peer_order;
    godot::HashMap<int64_t, godot::Array> pending;

public:
    void append(int64_t peer_id, const InterestAwareness &edge);

    godot::PackedInt64Array targets() const;

    godot::Array wire_for(int64_t peer_id) const;

    void forget(int64_t peer_id);
    bool is_empty() const;
    void clear();
};

} // namespace netw
