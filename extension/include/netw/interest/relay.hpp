#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::interest {

struct Awareness {
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

    static Awareness layer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int kind
    );
    static Awareness observer_edge(
        int64_t route,
        const godot::StringName &layer_id,
        int64_t observer_peer,
        int kind
    );

    static bool from_array(const godot::Variant &raw, Awareness &r_edge);
    godot::Array to_array() const;
};

struct AwarenessEdge {
    bool observer_scoped = false;
    uint64_t route = 0;
    godot::StringName layer_id;
    uint64_t observer = 0;
    bool entered = false;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&AwarenessEdge::observer_scoped>(
            "observer_scoped",
            netw::wire::bool1()
        ),
        netw::wire::field<&AwarenessEdge::route>(
            "route",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&AwarenessEdge::layer_id>(
            "layer_id",
            netw::wire::string()
        ),
        netw::wire::field<&AwarenessEdge::observer>(
            "observer",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&AwarenessEdge::entered>(
            "entered",
            netw::wire::bool1()
        )
    );
};

godot::PackedByteArray awareness_encode(const godot::Array &edges);

bool awareness_decode(
    const godot::PackedByteArray &bytes,
    godot::LocalVector<Awareness> &r_edges
);

godot::Dictionary awareness_spec_records();

class Relay {
    godot::LocalVector<int64_t> peer_order;
    godot::HashMap<int64_t, godot::Array> pending;

public:
    void append(int64_t peer_id, const Awareness &edge);

    godot::PackedInt64Array targets() const;

    godot::Array wire_for(int64_t peer_id) const;

    void forget(int64_t peer_id);
    bool is_empty() const;
    void clear();
};

} // namespace netw::interest
