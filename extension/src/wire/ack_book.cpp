#include "netw/wire/ack_book.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

namespace {

int slot_of(uint16_t seq) {
    return seq % AckBook::MAX_IN_FLIGHT;
}

} // namespace

bool AckBook::record_send(uint16_t seq, uint16_t frames, int64_t bits) {
    const int slot = slot_of(seq);
    if (active[slot] && ring[slot].seq != seq) {
        NETW_WARN_ONCE(
            sys::WIRE,
            "Sequence %d finds its ack slot held by %d, so this send is "
            "untracked.",
            int(seq),
            int(ring[slot].seq)
        );
        return false;
    }
    ring[slot].seq = seq;
    ring[slot].frames = frames;
    ring[slot].bits = bits;
    active[slot] = true;
    return true;
}

bool AckBook::take(uint16_t seq, AckEntry &r_entry) {
    const int slot = slot_of(seq);
    if (!active[slot] || ring[slot].seq != seq) {
        return false;
    }
    r_entry = ring[slot];
    active[slot] = false;
    return true;
}

void AckBook::process_ack(
    uint16_t ack_seq,
    uint32_t history,
    godot::LocalVector<AckEntry> &out_delivered,
    godot::LocalVector<AckEntry> &out_lost
) {
    NETW_ZONE_NC("AckBook process ack", colors::WIRE);
    out_delivered.clear();
    out_lost.clear();

    AckEntry taken;
    if (take(ack_seq, taken)) {
        out_delivered.push_back(taken);
    }
    for (int back = 0; back < HISTORY_DEPTH; ++back) {
        if ((history & (uint32_t(1) << back)) == 0) {
            continue;
        }
        if (take(uint16_t(ack_seq - 1 - back), taken)) {
            out_delivered.push_back(taken);
        }
    }

    if (!has_acked || static_cast<int16_t>(ack_seq - highest_acked_seq) > 0) {
        highest_acked_seq = ack_seq;
        has_acked = true;
    }

    for (int i = 0; i < MAX_IN_FLIGHT; ++i) {
        if (!active[i]) {
            continue;
        }
        const int16_t diff
            = static_cast<int16_t>(highest_acked_seq - ring[i].seq);
        if (diff > HISTORY_DEPTH) {
            out_lost.push_back(ring[i]);
            active[i] = false;
        }
    }
}

void AckBook::clear() {
    for (int i = 0; i < MAX_IN_FLIGHT; ++i) {
        active[i] = false;
    }
    has_acked = false;
    highest_acked_seq = 0;
}

int AckBook::active_count() const {
    int count = 0;
    for (int i = 0; i < MAX_IN_FLIGHT; ++i) {
        if (active[i]) {
            count++;
        }
    }
    return count;
}

} // namespace netw::wire
