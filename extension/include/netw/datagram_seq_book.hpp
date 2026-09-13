#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class DatagramSeqBook {
private:
    godot::HashMap<int64_t, uint16_t> send_seqs;
    godot::HashMap<int64_t, uint16_t> inbound_freshest;
    godot::HashMap<int64_t, uint32_t> inbound_history;
    godot::HashMap<int64_t, uint16_t> peer_acks;
    godot::HashMap<int64_t, uint32_t> peer_ack_histories;
    godot::HashMap<int64_t, uint16_t> last_echoed;
    godot::HashMap<int64_t, int64_t> reorders;
    godot::HashMap<int64_t, int64_t> duplicates;

public:
    static bool is_fresher(uint16_t a, uint16_t b);
    static int64_t distance(uint16_t newer, uint16_t older);

    uint16_t next_send_seq(int64_t peer);

    bool has_inbound(int64_t peer) const;
    uint16_t inbound_seq(int64_t peer) const;
    uint32_t inbound_delivery_history(int64_t peer) const;
    bool note_inbound(int64_t peer, uint16_t seq);
    int64_t reorder_count(int64_t peer) const;
    int64_t duplicate_count(int64_t peer) const;

    bool note_peer_ack(int64_t peer, uint16_t ack, uint32_t history);
    int64_t peer_ack(int64_t peer) const;
    uint32_t peer_ack_history(int64_t peer) const;

    void note_echoed(int64_t peer, uint16_t seq);
    godot::PackedInt64Array peers_owed_echo() const;

    void forget_peer(int64_t peer);
    void clear();
};

} // namespace netw
