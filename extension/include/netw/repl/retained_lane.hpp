#pragma once

/* A reliable row's send side, where delivery is ordered and guaranteed.
 *
 * The same diff as the masked lane and one decisive difference: this lane's
 * channel is RELIABLE, so a row that was sent will arrive, in order. That
 * changes when a baseline may advance, and it is the whole reason this is a
 * separate component rather than a flag on the other one.
 *
 * On the unreliable lane a baseline may only advance when an ack PROVES the
 * receiver reconstructed the row, and columns stay sticky until it does,
 * because a datagram that never landed would otherwise be diffed against.
 *
 * Here the send is the proof. Advancing on send costs nothing that can be
 * lost, and waiting for an ack instead would keep every column sticky across
 * a round trip for no gain: the lane would send each change twice as a matter
 * of course.
 *
 * Getting the two the wrong way round is expensive in both directions and
 * silent in one. A retained lane treated as unreliable pays double for every
 * change forever. A masked lane treated as reliable diffs against rows that
 * never arrived, and the columns it then stops sending are gone with no later
 * frame to correct them.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

class RetainedLane {
    struct Peer {
        wire::CodeRow held;
        bool has_held = false;
    };

    wire::WirePlan compiled;
    godot::Ref<SchemaRecord> declaration;
    godot::HashMap<int, Peer> peers;

public:
    static RetainedLane open(const wire::WirePlan &p_plan);

    // Opens against the declaration a caller holds, so the same lane both
    // grids values and diffs them. A lane opened from a plan alone cannot
    // gather, because a plan fixes widths and a schema fixes meaning.
    static RetainedLane declare(const godot::Ref<SchemaRecord> &p_schema);

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    bool knows(int p_peer) const;

    /* The columns `p_peer` is owed of `p_row`, and the baseline advance.
     *
     * One call, because the send is the proof: a caller that could ask without
     * advancing would be able to compute a mask, fail to send it, and leave
     * the lane believing the peer holds what it does not.
     */
    uint64_t send(int p_peer, const wire::CodeRow &p_row);

    // Forgets every peer outside `p_recipients`, so a peer returning to the
    // lane is sent the whole row rather than a diff against what it held
    // before it left.
    void retain(const godot::LocalVector<int> &p_recipients);

    void forget(int p_peer);
};

} // namespace netw::repl
