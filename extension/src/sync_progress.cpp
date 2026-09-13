#include "netw/sync_progress.hpp"

namespace netw {

bool SyncProgress::accept_unreliable(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    int64_t p_sequence
) {
    return impl.accept(
        int(p_sender),
        p_route,
        uint8_t(p_channel),
        uint16_t(p_sequence)
    );
}

void SyncProgress::open_datagram() {
    impl.open_datagram();
}

bool SyncProgress::accept_in_datagram(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    int64_t p_sequence
) {
    return impl.accept_in_datagram(
        int(p_sender),
        p_route,
        uint8_t(p_channel),
        uint16_t(p_sequence)
    );
}

void SyncProgress::clear_route(int64_t p_route) {
    impl.clear_route(p_route);
}

void SyncProgress::clear_peer(int64_t p_peer) {
    impl.clear_peer(int(p_peer));
}

void SyncProgress::clear() {
    impl.clear();
}

int64_t SyncProgress::stream_count() const {
    return int64_t(impl.stream_count());
}

int64_t SyncProgress::stale_count() const {
    return int64_t(impl.stale_count());
}

} // namespace netw
