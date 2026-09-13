#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"

namespace netw::repl {

class LinkGovernor {
public:
    enum Mode : uint8_t { GOOD, BAD };

    static constexpr uint32_t WINDOW = 32;
    static constexpr uint32_t MIN_SAMPLES = 8;
    static constexpr float LOSS_ENTER = 0.05f;
    static constexpr double RTT_EXCESS_MS = 250.0;
    static constexpr int64_t CLEAN_TICKS = 64;
    static constexpr int64_t CLEAN_CAP = 512;
    static constexpr int64_t FLAP_TICKS = 3600;
    static constexpr int64_t FLOOR_BITS = 128 * 8;

private:
    struct Peer {
        uint32_t outcomes = 0;
        uint32_t filled = 0;
        Mode mode = GOOD;
        int64_t clean_since = -1;
        int64_t clean_needed = CLEAN_TICKS;
        int64_t left_bad_at = -1;
        double rtt_floor_ms = -1.0;
        double rtt_ms = 0.0;
        double jitter_ms = 0.0;
    };

    godot::HashMap<int, Peer> peers;

    static void push(Peer &r_peer, bool p_lost);
    static bool strained(const Peer &p_peer);

public:
    void note_ack(
        int p_peer,
        uint32_t p_delivered,
        uint32_t p_lost,
        double p_rtt_ms,
        double p_jitter_ms,
        int64_t p_tick
    );

    Mode mode(int p_peer) const;
    float loss(int p_peer) const;
    double rtt_ms(int p_peer) const;
    double jitter_ms(int p_peer) const;

    int64_t budget_bits(int p_peer, int64_t p_full_bits) const;

    void forget(int p_peer);
    void clear();
};

} // namespace netw::repl
