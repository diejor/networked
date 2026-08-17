#pragma once

/* One datagram's frames, admitted and applied in the order they arrived.
 *
 * The barrier is PER FRAME, not per datagram, and that is a decision rather
 * than an accident of structure. A row that fails halfway writes none of
 * itself, because a half-applied row is a state that never existed on the
 * sender. But frames in one datagram address different entities, so refusing
 * the whole datagram over one bad frame would let a single malformed frame
 * stall every unrelated entity that happened to share it, which is a denial of
 * service a sender can arrange for free.
 *
 * So a refused frame is refused alone, its verdict is recorded, and the rest
 * of the datagram proceeds. The verdicts come back one per frame in arrival
 * order, because a caller that cannot say WHICH frame was refused cannot
 * attribute a drop to a sender.
 *
 * Application order is arrival order. Nothing here sorts, buckets, or defers
 * between frames: two runs over one datagram apply the same frames in the same
 * order, which is what lets a capture be replayed and compared at all.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/wire/registry.hpp"

namespace netw::repl {

// One frame as it arrived, before anything has decided whether it may be read.
struct InboundFrame {
    uint8_t channel = 0;
    int64_t route = 0;
    uint8_t comp = 0;
    int64_t sender = 0;
    bool payload_empty = true;
};

struct ReceiveResult {
    // One verdict per frame, in arrival order. OK means the frame was admitted
    // and handed to the applier.
    godot::LocalVector<godot::Error> verdicts;
    uint32_t admitted = 0;
    uint32_t refused = 0;
};

/* Whether one inbound frame may be read at all.
 *
 * Separated from the applier so a caller can be refused without the applier
 * ever seeing the frame: a gate that ran after the read would have already
 * spent the work it exists to refuse.
 */
using AdmitFn = godot::Error (*)(const InboundFrame &, void *);
using ApplyFn = godot::Error (*)(const InboundFrame &, void *);

class ReceivePass {
public:
    // Admits each frame and applies the ones that pass, in arrival order.
    // `p_context` is handed to both callbacks untouched, which is what keeps
    // this free of any session, clock or tree.
    static ReceiveResult run(
        const godot::LocalVector<InboundFrame> &p_frames,
        AdmitFn p_admit,
        ApplyFn p_apply,
        void *p_context
    );
};

} // namespace netw::repl
