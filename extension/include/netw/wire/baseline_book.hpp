#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

// What each peer is provably known to hold of one replicated row, and what it
// has been told since.
//
// A masked send carries only the columns that moved, so the sender needs an
// exact model of the receiver to diff against. That model is the CONFIRMED
// baseline: the last row a peer's own datagram ack proves it reconstructed. A
// staged row is not a baseline, because the datagram carrying it may never
// land, and diffing against a row the peer never received would silence the
// very columns that never arrived.
//
// Two rules make the model safe, and they are the reason this is a component
// rather than a diff.
//
// A column stays sent until an ack promotes a baseline carrying it. Without
// that, a value that moves away from the confirmed baseline and back inside
// the in-flight window diffs clean against the stale baseline, and the interim
// value the receiver was handed is stranded with no later frame to correct it.
//
// A peer with no confirmed baseline is sent every column. A peer that just
// became a recipient has reconstructed nothing, so the diff against its absent
// baseline is a whole row rather than an empty one, and that is the gain edge's
// heal.
//
// Every row here is built for one plan, so a book serves one schema and two of
// its rows are always comparable.
class BaselineBook {
    struct Staged {
        uint16_t seq = 0;
        CodeRow row;
    };

    struct Peer {
        CodeRow confirmed;
        bool has_confirmed = false;
        // Columns sent since `confirmed` last advanced, one bit per column in
        // plan order.
        uint64_t sticky = 0;
        godot::LocalVector<Staged> in_flight;
    };

    godot::HashMap<int, Peer> peers;

public:
    // Rows one peer may hold staged before an ack decides their fate. The
    // bound is what keeps the seq comparison sound: a datagram seq is a u16
    // read across a half window, so an entry old enough to have wrapped would
    // compare as FRESHER than the ack that should retire it. Staging past this
    // depth drops the oldest, which at worst forgoes a promotion to a row the
    // peer has already superseded.
    static constexpr uint32_t MAX_IN_FLIGHT = 64;

    // The columns `peer` must be sent of `row`: those whose codes differ from
    // the baseline it is confirmed to hold, plus every column already sent
    // since that baseline last advanced. An empty mask means a caught-up peer,
    // which costs this pass nothing.
    //
    // The columns it names become sticky, so this is the call that arms the
    // rule above. `row` is the row one pump polled and every recipient shares.
    uint64_t mask_to_send(int peer, const WirePlan &plan, const CodeRow &row);

    // Records `row` as what `peer` will hold if the datagram stamped `seq`
    // lands.
    void stage(int peer, uint16_t seq, const CodeRow &row);

    // Advances `peer`'s confirmed baseline to the freshest row staged at or
    // before `acked_seq`, drops every staged row at or before it, and clears
    // the sticky columns, since a baseline the receiver reconstructed is
    // sufficient to diff against.
    //
    // A peer with nothing staged at or before `acked_seq` keeps the baseline
    // it had. A stalled peer is diffed against an older truth rather than
    // against a corrupted one.
    void acknowledge(int peer, uint16_t acked_seq);

    // Forgets every peer outside `recipients`, so a baseline never outlives
    // the visibility that earned it and a peer returning to the route heals
    // with a whole row.
    void retain(const godot::LocalVector<int> &recipients);

    void forget(int peer);
    void clear();

    bool has_baseline(int peer) const;
    uint32_t in_flight_count(int peer) const;
};

} // namespace netw::wire
