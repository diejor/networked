#include "netw/sync_progress.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

bool NetwSyncProgress::accept_unreliable(
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

void NetwSyncProgress::clear_route(int64_t p_route) {
    impl.clear_route(p_route);
}

void NetwSyncProgress::clear_peer(int64_t p_peer) {
    impl.clear_peer(int(p_peer));
}

void NetwSyncProgress::clear() {
    impl.clear();
}

Dictionary NetwSyncProgress::stats() const {
    Dictionary out;
    out[StringName("streams")] = int64_t(impl.stream_count());
    return out;
}

void NetwSyncProgress::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("accept_unreliable", "sender", "route", "channel", "sequence"),
        &NetwSyncProgress::accept_unreliable
    );
    ClassDB::bind_method(
        D_METHOD("clear_route", "route"),
        &NetwSyncProgress::clear_route
    );
    ClassDB::bind_method(
        D_METHOD("clear_peer", "peer"),
        &NetwSyncProgress::clear_peer
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwSyncProgress::clear);
    ClassDB::bind_method(D_METHOD("stats"), &NetwSyncProgress::stats);
}

} // namespace netw
