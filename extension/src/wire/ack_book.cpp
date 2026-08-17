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

bool AckBook::record_send(uint16_t seq, uint8_t channel_id, int64_t send_id) {
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
    ring[slot].channel_id = channel_id;
    ring[slot].send_id = send_id;
    active[slot] = true;
    return true;
}

void AckBook::process_ack(
    uint16_t ack_seq,
    godot::LocalVector<AckEntry> &out_delivered,
    godot::LocalVector<AckEntry> &out_lost
) {
    NETW_ZONE_NC("AckBook process ack", colors::WIRE);
    out_delivered.clear();
    out_lost.clear();

    const int ack_slot = slot_of(ack_seq);
    if (active[ack_slot] && ring[ack_slot].seq == ack_seq) {
        out_delivered.push_back(ring[ack_slot]);
        active[ack_slot] = false;
    }

    if (!has_acked || static_cast<int16_t>(ack_seq - highest_acked_seq) > 0) {
        highest_acked_seq = ack_seq;
        has_acked = true;
    }

    for (int i = 0; i < MAX_IN_FLIGHT; ++i) {
        if (!active[i]) {
            continue;
        }
        const int16_t diff = static_cast<int16_t>(highest_acked_seq - ring[i].seq);
        if (diff > 32) {
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
