#include "netw/datagram_seq_book.hpp"

using namespace godot;

namespace netw {

bool DatagramSeqBook::is_fresher(uint16_t a, uint16_t b) {
    return a != b && uint16_t(a - b) < 32768;
}

int64_t DatagramSeqBook::distance(uint16_t newer, uint16_t older) {
    return int64_t(uint16_t(newer - older));
}

uint16_t DatagramSeqBook::next_send_seq(int64_t peer, uint64_t now_usec) {
    uint16_t *held = send_seqs.getptr(peer);
    const uint16_t next = uint16_t((held ? *held : uint16_t(0)) + 1);
    send_seqs[peer] = next;
    RoundTrip &trip = round_trips[peer];
    const int slot = next % SENT_WINDOW;
    trip.seqs[slot] = next;
    trip.sent_usec[slot] = now_usec;
    trip.stamped[slot] = true;
    return next;
}

bool DatagramSeqBook::has_inbound(int64_t peer) const {
    return inbound_freshest.has(peer);
}

uint16_t DatagramSeqBook::inbound_seq(int64_t peer) const {
    const uint16_t *held = inbound_freshest.getptr(peer);
    return held ? *held : uint16_t(0);
}

uint32_t DatagramSeqBook::inbound_delivery_history(int64_t peer) const {
    const uint32_t *held = inbound_history.getptr(peer);
    return held ? *held : uint32_t(0);
}

bool DatagramSeqBook::note_inbound(int64_t peer, uint16_t seq) {
    const uint16_t *held = inbound_freshest.getptr(peer);
    if (held == nullptr) {
        inbound_freshest[peer] = seq;
        inbound_history[peer] = 0;
        return true;
    }
    const uint16_t freshest = *held;
    uint32_t history = inbound_delivery_history(peer);
    if (is_fresher(seq, freshest)) {
        const int64_t step = distance(seq, freshest);
        history = step >= 32 ? uint32_t(0)
                             : uint32_t((history << step) | (1u << (step - 1)));
        inbound_freshest[peer] = seq;
        inbound_history[peer] = history;
        return true;
    }
    const int64_t back = distance(freshest, seq);
    if (back == 0) {
        duplicates[peer] += 1;
        return false;
    }
    if (back >= 1 && back <= 32) {
        const uint32_t bit = 1u << (back - 1);
        if ((history & bit) != 0) {
            duplicates[peer] += 1;
        } else {
            reorders[peer] += 1;
            inbound_history[peer] = history | bit;
        }
    }
    return false;
}

int64_t DatagramSeqBook::reorder_count(int64_t peer) const {
    const int64_t *held = reorders.getptr(peer);
    return held ? *held : 0;
}

int64_t DatagramSeqBook::duplicate_count(int64_t peer) const {
    const int64_t *held = duplicates.getptr(peer);
    return held ? *held : 0;
}

bool DatagramSeqBook::note_peer_ack(
    int64_t peer,
    uint16_t ack,
    uint32_t history,
    uint64_t now_usec
) {
    const uint16_t *held = peer_acks.getptr(peer);
    if (held && !is_fresher(ack, *held)) {
        return false;
    }
    peer_acks[peer] = ack;
    peer_ack_histories[peer] = history;

    RoundTrip *trip = round_trips.getptr(peer);
    const int slot = ack % SENT_WINDOW;
    if (trip == nullptr || !trip->stamped[slot] || trip->seqs[slot] != ack
        || now_usec < trip->sent_usec[slot]) {
        return true;
    }
    trip->stamped[slot] = false;
    const double sample = double(now_usec - trip->sent_usec[slot]) / 1000.0;
    if (!trip->measured) {
        trip->smoothed_ms = sample;
        trip->jitter_ms = sample / 2.0;
        trip->measured = true;
        return true;
    }
    const double deviation = sample > trip->smoothed_ms
        ? sample - trip->smoothed_ms
        : trip->smoothed_ms - sample;
    trip->jitter_ms += (deviation - trip->jitter_ms) / 4.0;
    trip->smoothed_ms += (sample - trip->smoothed_ms) / 8.0;
    return true;
}

int64_t DatagramSeqBook::peer_ack(int64_t peer) const {
    const uint16_t *held = peer_acks.getptr(peer);
    return held ? int64_t(*held) : -1;
}

uint32_t DatagramSeqBook::peer_ack_history(int64_t peer) const {
    const uint32_t *held = peer_ack_histories.getptr(peer);
    return held ? *held : uint32_t(0);
}

double DatagramSeqBook::round_trip_ms(int64_t peer) const {
    const RoundTrip *trip = round_trips.getptr(peer);
    return trip != nullptr && trip->measured ? trip->smoothed_ms : 0.0;
}

double DatagramSeqBook::round_trip_jitter_ms(int64_t peer) const {
    const RoundTrip *trip = round_trips.getptr(peer);
    return trip != nullptr && trip->measured ? trip->jitter_ms : 0.0;
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
    round_trips.erase(peer);
    inbound_freshest.erase(peer);
    inbound_history.erase(peer);
    peer_acks.erase(peer);
    peer_ack_histories.erase(peer);
    last_echoed.erase(peer);
    reorders.erase(peer);
    duplicates.erase(peer);
}

void DatagramSeqBook::clear() {
    send_seqs.clear();
    round_trips.clear();
    inbound_freshest.clear();
    inbound_history.clear();
    peer_acks.clear();
    peer_ack_histories.clear();
    last_echoed.clear();
    reorders.clear();
    duplicates.clear();
}

} // namespace netw
