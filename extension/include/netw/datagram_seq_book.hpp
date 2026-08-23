#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class DatagramSeqBook {
private:
    godot::HashMap<int64_t, uint16_t> send_seqs;
    godot::HashMap<int64_t, uint16_t> inbound_freshest;
    godot::HashMap<int64_t, uint16_t> peer_acks;
    godot::HashMap<int64_t, uint16_t> last_echoed;

public:
    static bool is_fresher(uint16_t a, uint16_t b);

    uint16_t next_send_seq(int64_t peer);

    bool has_inbound(int64_t peer) const;
    uint16_t inbound_seq(int64_t peer) const;
    bool note_inbound(int64_t peer, uint16_t seq);

    bool note_peer_ack(int64_t peer, uint16_t ack);
    int64_t peer_ack(int64_t peer) const;

    void note_echoed(int64_t peer, uint16_t seq);
    godot::PackedInt64Array peers_owed_echo() const;

    void forget_peer(int64_t peer);
    void clear();
};

} // namespace netw
