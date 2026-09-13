#pragma once

#include <cstdint>

#include "netw/repl/freshness_book.hpp"

namespace netw {

class SyncProgress {
    repl::FreshnessBook impl;

public:
    bool accept_unreliable(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        int64_t p_sequence
    );

    void open_datagram();

    bool accept_in_datagram(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        int64_t p_sequence
    );

    void clear_route(int64_t p_route);
    void clear_peer(int64_t p_peer);
    void clear();

    int64_t stream_count() const;
    int64_t stale_count() const;
};

} // namespace netw
