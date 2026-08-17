#include "netw/datagram_seq_book.hpp"

using namespace godot;

namespace netw {

bool DatagramSeqBook::is_fresher(uint16_t a, uint16_t b) {
    return a != b && uint16_t(a - b) < 32768;
}

uint16_t DatagramSeqBook::next_send_seq(int64_t peer) {
    uint16_t *held = send_seqs.getptr(peer);
    const uint16_t next = uint16_t((held ? *held : uint16_t(0)) + 1);
    send_seqs[peer] = next;
    return next;
}

bool DatagramSeqBook::has_inbound(int64_t peer) const {
    return inbound_freshest.has(peer);
}

uint16_t DatagramSeqBook::inbound_seq(int64_t peer) const {
    const uint16_t *held = inbound_freshest.getptr(peer);
    return held ? *held : uint16_t(0);
}

bool DatagramSeqBook::note_inbound(int64_t peer, uint16_t seq) {
    const uint16_t *held = inbound_freshest.getptr(peer);
    if (held && !is_fresher(seq, *held)) {
        return false;
    }
    inbound_freshest[peer] = seq;
    return true;
}

bool DatagramSeqBook::note_peer_ack(int64_t peer, uint16_t ack) {
    const uint16_t *held = peer_acks.getptr(peer);
    if (held && !is_fresher(ack, *held)) {
        return false;
    }
    peer_acks[peer] = ack;
    return true;
}

int64_t DatagramSeqBook::peer_ack(int64_t peer) const {
    const uint16_t *held = peer_acks.getptr(peer);
    return held ? int64_t(*held) : -1;
}

void DatagramSeqBook::note_echoed(int64_t peer, uint16_t seq) {
    last_echoed[peer] = seq;
}

PackedInt64Array DatagramSeqBook::peers_owed_echo() const {
    PackedInt64Array owed;
    for (const KeyValue<int64_t, uint16_t> &row : inbound_freshest) {
        const uint16_t *echoed = last_echoed.getptr(row.key);
        if (echoed && *echoed == row.value) {
            continue;
        }
        owed.push_back(row.key);
    }
    return owed;
}

void DatagramSeqBook::forget_peer(int64_t peer) {
    send_seqs.erase(peer);
    inbound_freshest.erase(peer);
    peer_acks.erase(peer);
    last_echoed.erase(peer);
}

void DatagramSeqBook::clear() {
    send_seqs.clear();
    inbound_freshest.clear();
    peer_acks.clear();
    last_echoed.clear();
}

} // namespace netw
