#include "netw/rate_window.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwRateWindow::set_limit(int value) {
    limit = value;
}

int NetwRateWindow::get_limit() const {
    return limit;
}

void NetwRateWindow::set_tracked_peers(int value) {
    tracked_peers = value;
}

int NetwRateWindow::get_tracked_peers() const {
    return tracked_peers;
}

void NetwRateWindow::set_span_msec(int64_t value) {
    span_msec = value;
}

int64_t NetwRateWindow::get_span_msec() const {
    return span_msec;
}

void NetwRateWindow::set_exempt_peer(int value) {
    exempt_peer = value;
}

int NetwRateWindow::get_exempt_peer() const {
    return exempt_peer;
}

bool NetwRateWindow::exceeded(int peer_id, int64_t now_msec) {
    if (peer_id == exempt_peer) {
        return false;
    }
    const int64_t window_start = now_msec - span_msec;
    if (stamps.size() > tracked_peers) {
        prune(window_start);
    }
    LocalVector<int64_t> *held = stamps.getptr(peer_id);
    if (held == nullptr) {
        stamps.insert(peer_id, LocalVector<int64_t>());
        held = stamps.getptr(peer_id);
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

// Drops peers whose newest request fell out of the current window.
void NetwRateWindow::prune(int64_t window_start) {
    LocalVector<int32_t> stale;
    for (const KeyValue<int32_t, LocalVector<int64_t>> &entry : stamps) {
        const LocalVector<int64_t> &held = entry.value;
        if (held.is_empty() || held[held.size() - 1] < window_start) {
            stale.push_back(entry.key);
        }
    }
    for (const int32_t peer_id : stale) {
        stamps.erase(peer_id);
    }
}

void NetwRateWindow::clear() {
    stamps.clear();
}

void NetwRateWindow::_bind_methods() {
#define NETW_RATE_BIND(m_name, m_variant) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, "value"), \
        &NetwRateWindow::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwRateWindow::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(m_variant, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

    NETW_RATE_BIND(limit, Variant::INT);
    NETW_RATE_BIND(tracked_peers, Variant::INT);
    NETW_RATE_BIND(span_msec, Variant::INT);
    NETW_RATE_BIND(exempt_peer, Variant::INT);

#undef NETW_RATE_BIND

    ClassDB::bind_method(
        D_METHOD("exceeded", "peer_id", "now_msec"),
        &NetwRateWindow::exceeded
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwRateWindow::clear);
}

} // namespace netw
