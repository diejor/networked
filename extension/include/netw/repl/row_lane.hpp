#pragma once

/* One replicated row's send side, composed.
 *
 * The pieces below this each answer one question and hold no opinion about the
 * others: a plan compiles a schema, a gather grids values, a book remembers
 * what a peer is known to hold. A lane is where they become a send, and it
 * exists because the order they run in is itself a contract. Gathering after
 * the diff would diff against a row nobody polled; staging before the mask
 * would tell a peer it holds columns it was never sent.
 *
 * A lane serves ONE schema, so every row it handles is comparable to every
 * other. Two schemas are two lanes.
 *
 * The lane holds no clock, no peer list and no transport. It is told the
 * values, the recipients and the datagram seq, which is what lets a test drive
 * a hundred passes without a session and what keeps the send order a function
 * of its inputs.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

class RowLane {
    wire::WirePlan compiled;
    wire::BaselineBook baselines;
    godot::Ref<SchemaRecord> declaration;

public:
    static RowLane open(const godot::Ref<SchemaRecord> &p_schema);

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    // Grids `p_values` into a row for this lane's plan. Answers false and
    // leaves `r_row` alone when the values are not this schema's, because a
    // half-gathered row diffs as though the columns it never reached had not
    // moved.
    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    // The columns `p_peer` must be sent of `p_row`, and the call that arms the
    // sticky set. Not a query: asking twice in one pass tells the book the
    // columns went out twice.
    uint64_t mask_for(int p_peer, const wire::CodeRow &p_row);

    // Records what `p_peer` will hold if the datagram stamped `p_seq` lands.
    // Only ever called for a peer whose mask was non-empty this pass, since
    // staging a row that was not sent would promote a baseline the peer never
    // received.
    void stage(int p_peer, uint16_t p_seq, const wire::CodeRow &p_row);

    void acknowledge(int p_peer, uint16_t p_acked_seq);

    // Forgets every peer outside `p_recipients`, so a peer returning to the
    // lane heals with a whole row rather than a diff against what it held
    // before it left.
    void retain(const godot::LocalVector<int> &p_recipients);

    void forget(int p_peer) {
        baselines.forget(p_peer);
    }

    bool knows(int p_peer) const {
        return baselines.has_baseline(p_peer);
    }

    uint32_t in_flight(int p_peer) const {
        return baselines.in_flight_count(p_peer);
    }
};

} // namespace netw::repl
