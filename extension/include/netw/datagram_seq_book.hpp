#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

// The four per-peer datagram sequence books, and the one rule that governs all
// of them: a sequence only ever advances, judged across the u16 half window, so
// a datagram that arrives late can never roll a book backward.
//
// [codeblock]
// send      the next u16 this session stamps on a datagram to that peer
// inbound   the freshest datagram of theirs this session holds
// acked     the freshest datagram of ours that peer has confirmed holding,
//           which is the baseline a delta-against-baseline lane diffs against
// echoed    the freshest inbound seq already echoed back to that peer, so a
//           flush can tell who is still owed a standalone ack
// [/codeblock]
//
// The books hold no peer identity beyond the id, so a peer that has left is
// forgotten by id rather than by asking a transport that no longer knows it.
class DatagramSeqBook {
private:
    godot::HashMap<int64_t, uint16_t> send_seqs;
    godot::HashMap<int64_t, uint16_t> inbound_freshest;
    godot::HashMap<int64_t, uint16_t> peer_acks;
    godot::HashMap<int64_t, uint16_t> last_echoed;

public:
    // Whether `a` is fresher than `b` on the u16 ring, judged across the half
    // window so a wrap reads as an advance and a reorder does not.
    static bool is_fresher(uint16_t a, uint16_t b);

    // The next outbound sequence for this peer, wrapped at 16 bits. Taking it
    // advances the book, so two datagrams to one peer can never share a stamp.
    uint16_t next_send_seq(int64_t peer);

    bool has_inbound(int64_t peer) const;
    uint16_t inbound_seq(int64_t peer) const;
    // Whether this datagram was fresher than what the book already held. A
    // reordered one is dropped rather than recorded.
    bool note_inbound(int64_t peer, uint16_t seq);

    // Whether the ack advanced this peer's confirmed baseline. A stalled echo
    // from a silent peer holds its baseline rather than corrupting it, and the
    // false answer is what tells a caller not to promote anything.
    bool note_peer_ack(int64_t peer, uint16_t ack);
    // The freshest sequence this peer has confirmed holding, or -1 when it has
    // confirmed nothing. Negative rather than zero, because zero is a sequence
    // a peer can genuinely have acked.
    int64_t peer_ack(int64_t peer) const;

    void note_echoed(int64_t peer, uint16_t seq);
    // Every peer this session holds a fresher inbound sequence for than it has
    // echoed back. Answering it is the whole of the standalone-ack decision
    // that does not depend on a transport.
    godot::PackedInt64Array peers_owed_echo() const;

    void forget_peer(int64_t peer);
    void clear();
};

} // namespace netw
