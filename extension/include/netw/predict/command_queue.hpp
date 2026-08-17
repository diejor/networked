#pragma once

/* The decoded owner window a consuming server has yet to replay, keyed by
 * transition.
 *
 * The owner lane re-sends an overlapping window every frame so a lost datagram
 * costs nothing, which means most arrivals name a transition already held. A
 * held cell is never merged or replaced: the first arrival is what authority
 * committed to replaying, and a second one carrying different bytes would let
 * a late duplicate rewrite a transition the consume cursor may already have
 * passed.
 *
 * [codeblock]
 * queue.admit(transition, label, fresh, command);   // false when held
 * const CommandCell *cell = queue.cell(cursor);
 * const int depth = queue.depth_from(cursor);       // contiguous run
 * [/codeblock]
 *
 * The payload stays a Dictionary because it is keyed by the input set's own
 * field names, which the pool never declares and cannot order.
 */

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

namespace predict {

struct CommandCell {
    int64_t label = -1;
    bool fresh = false;
    Dictionary command;
};

class CommandQueue {
    HashMap<int64_t, CommandCell> cells;
    int64_t oldest = -1;
    int64_t newest = -1;

    void drop_oldest();

public:
    bool admit(
        int64_t p_transition,
        int64_t p_label,
        bool p_fresh,
        const Dictionary &p_command
    );

    bool has(int64_t p_transition) const;

    // Null for a transition the window does not answer for, which is the same
    // absence a cursor reading past the newest arrival already handles.
    const CommandCell *cell(int64_t p_transition) const;

    // Contiguous cells beginning exactly at p_cursor. A hole ends the run,
    // because the consume cursor may not step over a transition whose command
    // has not arrived.
    int depth_from(int64_t p_cursor) const;

    int size() const {
        return int(cells.size());
    }

    int64_t oldest_transition() const {
        return oldest;
    }

    int64_t newest_transition() const {
        return newest;
    }

    // Every transition held, ascending. The window gaps wherever a datagram
    // was lost, so a caller enumerating it reads this rather than the span.
    PackedInt64Array transitions() const;

    void clear();
};

} // namespace predict

} // namespace netw
