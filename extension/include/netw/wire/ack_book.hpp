#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"

namespace netw::wire {

struct AckEntry {
    uint16_t seq = 0;
    uint16_t frames = 0;
    int64_t bits = 0;
};

class AckBook {
public:
    static constexpr int MAX_IN_FLIGHT = 128;
    static constexpr int HISTORY_DEPTH = 32;

private:
    AckEntry ring[MAX_IN_FLIGHT];
    bool active[MAX_IN_FLIGHT] = {false};
    uint16_t highest_acked_seq = 0;
    bool has_acked = false;

    bool take(uint16_t seq, AckEntry &r_entry);

public:
    bool record_send(uint16_t seq, uint16_t frames, int64_t bits);

    void process_ack(
        uint16_t ack_seq,
        uint32_t history,
        godot::LocalVector<AckEntry> &out_delivered,
        godot::LocalVector<AckEntry> &out_lost
    );

    void clear();
    int active_count() const;
};

} // namespace netw::wire
