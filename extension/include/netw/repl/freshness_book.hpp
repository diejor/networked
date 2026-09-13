#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

namespace netw::repl {

class FreshnessBook {
    struct DatagramVerdict {
        int64_t route = 0;
        uint64_t stream = 0;
        bool accepted = false;
    };

    godot::HashMap<int64_t, godot::HashMap<uint64_t, uint16_t>> routes;
    godot::LocalVector<DatagramVerdict> datagram;
    uint32_t stale = 0;

    static uint64_t stream(int p_sender, uint8_t p_channel) {
        return (uint64_t(uint32_t(p_sender)) << 8) | uint64_t(p_channel);
    }

public:
    bool accept(
        int p_sender,
        int64_t p_route,
        uint8_t p_channel,
        uint16_t p_seq
    );

    void open_datagram();

    bool accept_in_datagram(
        int p_sender,
        int64_t p_route,
        uint8_t p_channel,
        uint16_t p_seq
    );

    void clear_route(int64_t p_route);

    void clear_peer(int p_peer);

    void clear();

    uint32_t stream_count() const;

    uint32_t stale_count() const;
};

} // namespace netw::repl
