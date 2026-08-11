#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"

namespace netw::wire {

// Per-peer tracking of in-flight sends mapped to sequence numbers for acking.
struct AckEntry {
    uint16_t seq = 0;
    uint8_t channel_id = 0;
    int64_t send_id = 0;
};

class AckBook {
public:
    static constexpr int MAX_IN_FLIGHT = 128;

private:
    AckEntry ring[MAX_IN_FLIGHT];
    bool active[MAX_IN_FLIGHT] = { false };
    uint16_t highest_acked_seq = 0;
    bool has_acked = false;

public:
    // Records a pending send tied to a datagram sequence number.
    bool record_send(uint16_t seq, uint8_t channel_id, int64_t send_id);

    // Processes an inbound ack sequence. Returns delivered and lost sends.
    void process_ack(
        uint16_t ack_seq,
        godot::LocalVector<AckEntry> &out_delivered,
        godot::LocalVector<AckEntry> &out_lost
    );

    void clear();
    int active_count() const;
};

} // namespace netw::wire
