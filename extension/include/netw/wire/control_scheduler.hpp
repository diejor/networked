#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "netw/wire/control_record.hpp"

namespace netw::wire {

constexpr int64_t CONTROL_FLUSH_PERIOD_MS = 50;
constexpr int64_t CONTROL_TICK_RESERVATION_BYTES = 256;

uint32_t varuint_byte_span(uint64_t p_value);

class ControlScheduler {
    struct Peer {
        godot::LocalVector<uint64_t> tokens;
        godot::LocalVector<uint64_t> revisions;
        godot::LocalVector<ControlRecord> lifecycle;
        int64_t flushed_at_ms = 0;
        bool primed = false;
    };

    godot::HashMap<int, Peer> peers;

public:
    void accept(int p_peer, uint64_t p_token, uint64_t p_revision);

    void queue(int p_peer, const ControlRecord &p_record);

    void drop_token(int p_peer, uint64_t p_token);

    bool due(int p_peer, int64_t p_now_ms) const;

    godot::LocalVector<int> ready_peers(int64_t p_now_ms) const;

    godot::LocalVector<ControlRecord> flush(
        int p_peer,
        int64_t p_now_ms,
        int64_t p_budget_bytes
    );

    void forget_peer(int p_peer);

    void clear();

    uint32_t pending_receipts(int p_peer) const;

    uint32_t pending_lifecycle(int p_peer) const;
};

} // namespace netw::wire
