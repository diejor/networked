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
    static constexpr int64_t FLOOR_BITS = 128 * 8;
    static constexpr float STEP = 0.125f;
    static constexpr float LEAST = 0.0625f;
    static constexpr int64_t ROUND_FLOOR_MS = 16;
    static constexpr int64_t CEILING_HOLD_MS = 60000;
    static constexpr uint32_t BACKLOG_GROWTH = 2;

private:
    struct Peer {
        uint32_t outcomes = 0;
        uint32_t filled = 0;
        float share = 1.0f;
        float ceiling = 1.0f;
        bool decreased = false;
        int64_t decreased_at_ms = 0;
        int64_t stepped_at_ms = 0;
        int64_t lost_at_ms = 0;
        int64_t edge_at_ms = -1;
        float edge_share = 1.0f;
        double rtt_floor_ms = -1.0;
        double rtt_ms = 0.0;
        double jitter_ms = 0.0;
        int64_t backlog = 0;
        uint32_t backlog_growth = 0;
        uint32_t delivered_total = 0;
    };

    godot::HashMap<int, Peer> peers;

    static void push(Peer &r_peer, bool p_lost);
    static bool slowed(const Peer &p_peer);
    static bool lossy(const Peer &p_peer);
    static int64_t round_ms(const Peer &p_peer);
    static void decrease(Peer &r_peer, int64_t p_now_ms, bool p_lossy);
    static void recover(Peer &r_peer, int64_t p_now_ms);

public:
    void note_ack(
        int p_peer,
        uint32_t p_delivered,
        uint32_t p_lost,
        double p_rtt_ms,
        double p_jitter_ms,
        int64_t p_now_ms
    );

    void note_backlog(int p_peer, int64_t p_buffered, int64_t p_now_ms);

    Mode mode(int p_peer) const;
    float share(int p_peer) const;
    float loss(int p_peer) const;
    double rtt_ms(int p_peer) const;
    double jitter_ms(int p_peer) const;

    int64_t budget_bits(int p_peer, int64_t p_full_bits) const;

    void forget(int p_peer);
    void clear();
};

} // namespace netw::repl
