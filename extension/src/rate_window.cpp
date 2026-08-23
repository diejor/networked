#include "netw/rate_window.hpp"

using namespace godot;

namespace netw {

bool RateWindow::exceeded(int peer_id, int64_t now_msec) {
    if (peer_id == exempt_peer) {
        return false;
    }
    const int64_t window_start = now_msec - span_msec;
    if (ascending_stamps.size() > tracked_peers) {
        drop_peers_idle_since(window_start);
    }
    LocalVector<int64_t> *held = ascending_stamps.getptr(peer_id);
    if (held == nullptr) {
        ascending_stamps.insert(peer_id, LocalVector<int64_t>());
        held = ascending_stamps.getptr(peer_id);
    }
    uint32_t expired = 0;
    while (expired < held->size() && (*held)[expired] < window_start) {
        ++expired;
    }
    if (expired > 0) {
        LocalVector<int64_t> kept;
        for (uint32_t index = expired; index < held->size(); ++index) {
            kept.push_back((*held)[index]);
        }
        *held = kept;
    }
    held->push_back(now_msec);
    return held->size() > uint32_t(limit);
}

void RateWindow::drop_peers_idle_since(int64_t window_start) {
    LocalVector<int32_t> stale;
    for (const KeyValue<int32_t, LocalVector<int64_t>> &entry :
         ascending_stamps) {
        const LocalVector<int64_t> &held = entry.value;
        if (held.is_empty() || held[held.size() - 1] < window_start) {
            stale.push_back(entry.key);
        }
    }
    for (const int32_t peer_id : stale) {
        ascending_stamps.erase(peer_id);
    }
}

void RateWindow::clear() {
    ascending_stamps.clear();
}

} // namespace netw
