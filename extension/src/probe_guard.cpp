#include "netw/probe_guard.hpp"

namespace netw {

bool ProbeGuard::admit(int64_t p_peer, int64_t p_now_ms) {
    active.insert(p_peer);
    const int64_t opened = p_now_ms - WINDOW_MS;
    godot::LocalVector<int64_t> kept;
    for (uint32_t at = 0; at < window.size(); at++) {
        if (window[at] >= opened) {
            kept.push_back(window[at]);
        }
    }
    kept.push_back(p_now_ms);
    window = kept;
    return int(window.size()) <= RATE_PER_SECOND
        && int(active.size()) <= MAX_ACTIVE;
}

bool ProbeGuard::forget(int64_t p_peer) {
    if (!active.has(p_peer)) {
        return false;
    }
    active.erase(p_peer);
    return true;
}

int ProbeGuard::active_count() const {
    return int(active.size());
}

int ProbeGuard::window_count() const {
    return int(window.size());
}

void ProbeGuard::clear() {
    window.clear();
    active.clear();
}

} // namespace netw
