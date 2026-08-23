#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

namespace netw::repl {

class FreshnessBook {
    godot::HashMap<int64_t, godot::HashMap<uint64_t, uint16_t>> routes;

    static uint64_t stream(int p_sender, uint8_t p_channel) {
        return (uint64_t(uint32_t(p_sender)) << 8) | uint64_t(p_channel);
    }

public:
    bool accept(int p_sender, int64_t p_route, uint8_t p_channel, uint16_t p_seq);

    void clear_route(int64_t p_route);

    void clear_peer(int p_peer);

    void clear();

    uint32_t stream_count() const;
};

} // namespace netw::repl
