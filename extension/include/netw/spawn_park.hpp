#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSpawnPark : public godot::RefCounted {
    GDCLASS(NetwSpawnPark, godot::RefCounted)

public:
    enum Wait {
        WAIT_ROUTE = 0,
        WAIT_SCENE = 1,
    };

private:
    struct Row {
        godot::PackedByteArray payload;
        int32_t wait = WAIT_ROUTE;
        int64_t deadline = 0;
    };

    godot::HashMap<int64_t, Row> rows;

protected:
    static void _bind_methods();

public:
    bool park(
        int64_t route,
        const godot::PackedByteArray &payload,
        Wait wait,
        int64_t deadline
    );

    bool has(int64_t route) const;

    godot::PackedByteArray take(int64_t route);

    bool cancel(int64_t route);

    godot::PackedInt64Array waiting_on(Wait wait) const;

    bool is_expired(int64_t route, int64_t now) const;

    static bool anchor_parks(int64_t state);

    int size() const;
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSpawnPark::Wait);
