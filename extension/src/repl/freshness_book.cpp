#include "netw/repl/freshness_book.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

bool FreshnessBook::accept(
    int p_sender,
    int64_t p_route,
    uint8_t p_channel,
    uint16_t p_seq
) {
    NETW_ZONE_NC("Freshness accept", colors::WIRE);
    HashMap<uint64_t, uint16_t> &held = routes[p_route];
    const uint64_t key = stream(p_sender, p_channel);
    HashMap<uint64_t, uint16_t>::Iterator last = held.find(key);
    if (last != held.end()) {
        const uint16_t seen = last->value;
        const uint16_t ahead = uint16_t(p_seq - seen);
        if (ahead == 0 || ahead >= 32768) {
            return false;
        }
    }
    held[key] = p_seq;
    return true;
}

void FreshnessBook::open_datagram() {
    datagram.clear();
}

bool FreshnessBook::accept_in_datagram(
    int p_sender,
    int64_t p_route,
    uint8_t p_channel,
    uint16_t p_seq
) {
    NETW_ZONE_NC("Freshness accept in datagram", colors::WIRE);
    const uint64_t key = stream(p_sender, p_channel);
    for (uint32_t at = 0; at < datagram.size(); ++at) {
        const DatagramVerdict &held = datagram[at];
        if (held.route == p_route && held.stream == key) {
            return held.accepted;
        }
    }
    const bool accepted = accept(p_sender, p_route, p_channel, p_seq);
    if (!accepted) {
        ++stale;
    }
    datagram.push_back(DatagramVerdict{p_route, key, accepted});
    return accepted;
}

void FreshnessBook::clear_route(int64_t p_route) {
    routes.erase(p_route);
}

void FreshnessBook::clear_peer(int p_peer) {
    NETW_ZONE_NC("Freshness forget peer", colors::WIRE);
    for (KeyValue<int64_t, HashMap<uint64_t, uint16_t>> &route : routes) {
        LocalVector<uint64_t> doomed;
        for (const KeyValue<uint64_t, uint16_t> &entry : route.value) {
            if (uint32_t(entry.key >> 8) == uint32_t(p_peer)) {
                doomed.push_back(entry.key);
            }
        }
        for (uint32_t at = 0; at < doomed.size(); ++at) {
            route.value.erase(doomed[at]);
        }
    }
}

void FreshnessBook::clear() {
    routes.clear();
    datagram.clear();
    stale = 0;
}

uint32_t FreshnessBook::stale_count() const {
    return stale;
}

uint32_t FreshnessBook::stream_count() const {
    uint32_t count = 0;
    for (const KeyValue<int64_t, HashMap<uint64_t, uint16_t>> &route : routes) {
        count += uint32_t(route.value.size());
    }
    return count;
}

} // namespace netw::repl
