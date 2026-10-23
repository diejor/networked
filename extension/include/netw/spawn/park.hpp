#pragma once

#include <cstdint>

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/liveness_core.hpp"

namespace netw::spawn {

class Park {
public:
    enum Wait : uint8_t {
        WAIT_NONE,
        WAIT_ROUTE,
        WAIT_SPAWNER,
        WAIT_ADOPT,
        WAIT_BUILD,
    };

private:
    struct Row {
        godot::PackedByteArray payload;
        Wait wait = WAIT_ROUTE;
        int64_t deadline = 0;
        int64_t sender = 0;
    };

    struct Reparent {
        godot::PackedByteArray payload;
        uint64_t revision = 0;
        int64_t sender = 0;
    };

    godot::HashMap<int64_t, Row> rows;
    godot::HashMap<int64_t, Reparent> reparents;
    godot::Ref<NetwLivenessCore> liveness;

public:
    void hold_routes_in(const godot::Ref<NetwLivenessCore> &p_liveness);

    bool park(
        int64_t route,
        const godot::PackedByteArray &payload,
        Wait wait,
        int64_t deadline,
        int64_t sender
    );

    bool has(int64_t route) const;

    godot::PackedByteArray take(int64_t route);

    godot::PackedByteArray peek(int64_t route) const;

    int64_t sender_of(int64_t route) const;

    bool cancel(int64_t route);

    bool keep_reparent(
        int64_t route,
        const godot::PackedByteArray &payload,
        uint64_t revision,
        int64_t sender
    );

    godot::PackedByteArray take_reparent(int64_t route, int64_t &r_sender);

    godot::PackedInt64Array waiting_on(Wait wait) const;

    bool is_expired(int64_t route, int64_t now) const;

    static bool anchor_parks(int64_t state);

    int size() const;
    void clear();
};

} // namespace netw::spawn
