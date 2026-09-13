#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/retained_lane.hpp"
#include "netw/repl/row_lane.hpp"
#include "netw/repl/window_ring.hpp"

namespace netw::repl {

using netw::table::SchemaRecord;

class LaneSet {
    static uint64_t address(int64_t p_route, uint8_t p_comp) {
        return (uint64_t(p_route) << 8) | uint64_t(p_comp);
    }

    godot::HashMap<uint64_t, RowLane> lanes;
    godot::HashMap<uint64_t, RetainedLane> retained;
    godot::HashMap<uint64_t, WindowRing> windows;

public:
    RowLane *open(
        int64_t p_route,
        uint8_t p_comp,
        const SchemaRecord &p_schema
    );

    RowLane *find(int64_t p_route, uint8_t p_comp);

    RetainedLane *open_retained(
        int64_t p_route,
        uint8_t p_comp,
        const SchemaRecord &p_schema
    );

    RetainedLane *find_retained(int64_t p_route, uint8_t p_comp);

    WindowRing *open_window(
        int64_t p_route,
        uint8_t p_comp,
        const SchemaRecord &p_schema,
        uint32_t p_depth
    );

    void close_route(int64_t p_route);

    void retain(const godot::LocalVector<int> &p_recipients);

    void retain_row(
        int64_t p_route,
        uint8_t p_comp,
        const godot::LocalVector<int> &p_recipients
    );

    void forget_peer(int p_peer);

    void acknowledge_peer(int p_peer, uint16_t p_acked_seq, uint32_t p_history);

    void clear();

    uint32_t size() const {
        return uint32_t(lanes.size());
    }

    uint32_t retained_size() const {
        return uint32_t(retained.size());
    }

    uint32_t window_size() const {
        return uint32_t(windows.size());
    }
};

} // namespace netw::repl
