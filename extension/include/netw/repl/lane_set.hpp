#pragma once

/* Every replicated row a session sends, one lane per addressed component.
 *
 * A lane remembers what each peer holds of ONE row. A session holds many, and
 * the things that are true of all of them at once are what this exists for.
 * Two in particular cannot be enforced one lane at a time:
 *
 * A peer that leaves the session has to be forgotten by EVERY lane, not by the
 * one that noticed. A lane still holding its baseline would diff against a
 * peer that is gone, and if that peer id is reused the new peer inherits the
 * old one's confirmed row and is sent a diff against something it never saw.
 *
 * A route that dies has to take its lanes with it. Routes are reused, so a
 * lane surviving its route hands the next entity at that address the baselines
 * of the last one.
 *
 * The volatile, retained and windowed lanes are three maps rather than one, so
 * the lanes of one declared set share an address without colliding. All three
 * die with their route. Only the two that hold a per-peer baseline are reached
 * by a peer's departure, and only the volatile one is reached by an ack: a
 * reliable send is its own proof, and a windowed lane holds no peer state at
 * all because every recipient is sent the same frame.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "netw/repl/retained_lane.hpp"
#include "netw/repl/row_lane.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/table/schema_core.hpp"

namespace netw::repl {

class LaneSet {
    // One address is a route and the component within it, packed so a lookup
    // is one hash rather than a walk over an entity's components.
    static uint64_t address(int64_t p_route, uint8_t p_comp) {
        return (uint64_t(p_route) << 8) | uint64_t(p_comp);
    }

    godot::HashMap<uint64_t, RowLane> lanes;
    godot::HashMap<uint64_t, RetainedLane> retained;
    godot::HashMap<uint64_t, WindowRing> windows;

public:
    // Opens the lane for this address, or answers the one already open. It
    // answers the existing lane rather than replacing it, because replacing
    // would discard every peer's confirmed baseline and silently send whole
    // rows to peers that were caught up.
    RowLane *open(
        int64_t p_route,
        uint8_t p_comp,
        const godot::Ref<SchemaRecord> &p_schema
    );

    RowLane *find(int64_t p_route, uint8_t p_comp);

    RetainedLane *open_retained(
        int64_t p_route,
        uint8_t p_comp,
        const godot::Ref<SchemaRecord> &p_schema
    );

    RetainedLane *find_retained(int64_t p_route, uint8_t p_comp);

    // Opens the windowed lane for this address at `p_depth`, or answers the
    // one already open. The depth of an open ring is left alone, because
    // shrinking it would drop samples the receiver may still be owed.
    WindowRing *open_window(
        int64_t p_route,
        uint8_t p_comp,
        const godot::Ref<SchemaRecord> &p_schema,
        uint32_t p_depth
    );

    // Drops every lane on `p_route`, so the next entity at that address starts
    // with no peer believed to hold anything.
    void close_route(int64_t p_route);

    // Forgets every peer outside `p_recipients`, in every lane.
    void retain(const godot::LocalVector<int> &p_recipients);

    void retain_row(
        int64_t p_route,
        uint8_t p_comp,
        const godot::LocalVector<int> &p_recipients
    );

    void forget_peer(int p_peer);

    // Advances `p_peer`'s confirmed baseline in every lane. An ack names a
    // datagram, and a datagram carries frames from many lanes, so the ack has
    // to reach all of them or the lanes it missed keep diffing against a row
    // the peer has already superseded.
    void acknowledge_peer(int p_peer, uint16_t p_acked_seq);

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
