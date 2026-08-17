#pragma once

/* One datagram's worth of send, decided.
 *
 * A pass takes what every lane offered, fits it to the budget, and records the
 * datagram so a later ack can settle it. The fitter and the ack book each have
 * their own laws; what a pass adds is that the two agree about one datagram,
 * which neither can check alone.
 *
 * TRACKING IS PER DATAGRAM, NOT PER FRAME, and that is forced rather than
 * chosen. An echo names the freshest datagram a peer has SEEN, so a datagram
 * is the finest thing an ack can speak about: a frame's fate is only ever
 * known through the datagram that carried it. The ack book is shaped to match,
 * one entry per seq slot, so a pass records once per datagram and the send id
 * it records names the datagram.
 *
 * THE REFUSAL IS THE REASON THIS IS A COMPONENT. `AckBook::record_send` can
 * refuse, because its ring is keyed by seq and a colliding live entry may not
 * be overwritten. A pass that sent the datagram anyway would put bytes on the
 * wire that nothing can ever ack or report lost, so the send would vanish from
 * every counter while arriving perfectly well. So a datagram the book will not
 * track is not sent: every frame in it is deferred, keeps the raised priority
 * a deferral earns, and rides a later seq the book can hold.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/ack_book.hpp"
#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace netw::repl {

struct PassResult {
    // What rides this datagram, in the order the fitter chose.
    godot::LocalVector<wire::FitCandidate> sent;
    // What did not, whether the budget refused it or the datagram was not
    // trackable. Each carries the raised priority a deferral earns.
    godot::LocalVector<wire::FitCandidate> deferred;
    int64_t sent_bits = 0;
    // True when the budget would have taken frames and the ack book would not
    // hold the datagram. Reported rather than hidden in `deferred`, because a
    // pass that keeps hitting this has a seq advancing slower than its sends
    // and will make no progress until that changes.
    bool untrackable = false;
};

class SendPass {
    wire::AckBook acks;

public:
    // Fits `p_offers` to `p_max_bits` and records the datagram under `p_seq`.
    // `p_offers` is reordered in place, as the fitter's contract requires.
    // `p_send_id` names the datagram, which is what an ack can settle.
    PassResult run(
        const wire::WireRegistry &p_registry,
        godot::LocalVector<wire::FitCandidate> &p_offers,
        int64_t p_max_bits,
        uint16_t p_seq,
        int64_t p_send_id
    );

    void acknowledge(
        uint16_t p_ack_seq,
        godot::LocalVector<wire::AckEntry> &r_delivered,
        godot::LocalVector<wire::AckEntry> &r_lost
    );

    int outstanding() const {
        return acks.active_count();
    }

    void clear() {
        acks.clear();
    }
};

} // namespace netw::repl
