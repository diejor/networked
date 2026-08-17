#pragma once

/* A masked row as bytes, and back.
 *
 * `SessionSend` decides WHICH columns a peer is owed. This is what carries
 * them: the address the row belongs to, the mask naming the columns present,
 * and those columns' code bits and no others. A frame is what the decision
 * becomes once it has to leave the process.
 *
 * ONLY THE MASKED COLUMNS RIDE. That is the whole point of a masked lane, and
 * it is also what makes the mask load-bearing rather than advisory: a reader
 * walks the plan and takes bits only where the mask says to, so a mask that
 * disagreed with the writer's would not merely mislabel the row, it would
 * desynchronize the stream and every column after it.
 *
 * A frame decodes to EXHAUSTION. A reader that stopped early would accept a
 * frame carrying bytes it never accounted for, and a sender could then hide
 * anything after a short row.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

// One row frame's header. The stamp and ack are carried per frame rather than
// per datagram because a datagram aggregates rows from different ticks.
struct RowFrameHeader {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    int64_t tick = -1;
    int64_t reconcile_ack = -1;
    uint64_t mask = 0;
};

// Writes `p_header` and the columns `p_header.mask` names of `p_row`. Answers
// an empty array when the mask is empty, because a caught-up peer costs the
// pass nothing and a frame carrying no columns is a frame worth no bytes.
godot::PackedByteArray write_row_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
);

/* Reads a frame written by the above, against the same plan.
 *
 * `r_row` receives the masked columns written into it and every other column
 * left as it was, which is what makes a masked frame applicable to the row a
 * receiver already holds. Answers false on a short frame, on residue, or on a
 * mask naming a column the plan does not have.
 */
bool read_row_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
);

bool peek_row_frame(
    const godot::PackedByteArray &p_bytes,
    RowFrameHeader &r_header
);

/* The same row with no mask, for a lane that sends every column to everyone.
 *
 * Most traffic is this. A plain lane does not diff, so every recipient gets one
 * frame and the mask would be the same full word on all of them: spending
 * `mask_width()` bits to say "all of it" on every frame of the commonest lane
 * is the one saving worth a second frame shape. Which shape a reader is holding
 * is decided by the channel, which the header already carries.
 *
 * `p_header.mask` is ignored on write and answered as the plan's full mask on
 * read, so a caller that mixes the two up gets a row rather than a surprise.
 */
godot::PackedByteArray write_plain_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
);

bool read_plain_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
);

/* Several ticks of one row in one frame, for a lane that repeats rather than
 * retransmits.
 *
 * The other three shapes carry the state of one moment. This one carries a
 * SEQUENCE, because its rows are not interchangeable: an input tick the
 * receiver missed is still owed a step, and a fresher one does not supersede
 * it. Repeating the range in flight heals the gap inside the next frame rather
 * than a round trip later.
 *
 * Each sample rides as its AGE below `p_header.tick` rather than as its own
 * tick, which is what keeps a frame's cost independent of how large the tick
 * counter has grown. A sample newer than the header is refused on write and on
 * read, because a negative age reconstructs a tick the sender never had.
 */
godot::PackedByteArray write_window_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const godot::LocalVector<WindowSample> &p_samples
);

bool read_window_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    godot::LocalVector<WindowSample> &r_samples
);

} // namespace netw::repl
