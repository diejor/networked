#include "netw/wire/stream_book.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

using namespace godot;

namespace {

uint64_t token_counter = 0;
uint64_t request_counter = 0;

constexpr int64_t REPAIR_FLOOR_MS = 250;
constexpr int64_t REPAIR_CEILING_MS = 1000;
constexpr uint32_t STRAY_MEMORY = 256;

bool anchor_superseded(const StreamTenure &p_lane, const StreamTenure &p_bar) {
    return p_lane.anchor < p_bar.anchor
        || (p_lane.anchor == p_bar.anchor
            && p_lane.anchor_author != p_bar.anchor_author);
}

StreamTenure anchor_bar(uint64_t p_anchor, uint64_t p_author) {
    StreamTenure bar;
    bar.anchor = p_anchor;
    bar.anchor_author = p_author;
    return bar;
}

} // namespace

uint64_t next_stream_token() {
    if (token_counter == UINT64_MAX) {
        NETW_ERR_V(
            0,
            sys::WIRE,
            "This process has used every stream id and cannot open another "
            "stream. Restart the process."
        );
    }
    return ++token_counter;
}

uint64_t next_open_request() {
    if (request_counter == UINT64_MAX) {
        NETW_ERR_V(
            0,
            sys::WIRE,
            "This process has used every stream request id and cannot open "
            "another stream. Restart the process."
        );
    }
    return ++request_counter;
}

bool binding_follows(
    const StreamTenure &p_origin,
    const StreamTenure &p_bound
) {
    return p_bound.bound == p_origin.bound && p_bound.tenure >= p_origin.tenure
        && p_bound.anchor == p_origin.anchor
        && p_bound.anchor_author == p_origin.anchor_author;
}

uint64_t binding_floor(
    const StreamTenure &p_origin,
    const StreamTenure &p_bound
) {
    return (p_bound.tenure - p_origin.tenure) * STREAM_BINDING_SPAN;
}

int64_t repair_interval_ms(int64_t p_smoothed_rtt_ms) {
    const int64_t doubled = p_smoothed_rtt_ms * 2;
    if (doubled < REPAIR_FLOOR_MS) {
        return REPAIR_FLOOR_MS;
    }
    if (doubled > REPAIR_CEILING_MS) {
        return REPAIR_CEILING_MS;
    }
    return doubled;
}

StreamReaderBook::Connection &StreamReaderBook::connection(int p_peer) {
    Connection *held = connections.getptr(p_peer);
    if (held != nullptr) {
        return *held;
    }
    uint64_t *counted = incarnations.getptr(p_peer);
    Connection fresh;
    fresh.incarnation = counted == nullptr ? 1 : *counted + 1;
    incarnations[p_peer] = fresh.incarnation;
    connections.insert(p_peer, fresh);
    return connections[p_peer];
}

uint64_t StreamReaderBook::incarnation_of(int p_peer) {
    return connection(p_peer).incarnation;
}

StreamReaderBook::Lane *StreamReaderBook::lane_at(
    int p_peer,
    uint64_t p_token
) {
    Connection *held = connections.getptr(p_peer);
    if (held == nullptr) {
        return nullptr;
    }
    const uint64_t *address = held->addresses.getptr(p_token);
    if (address == nullptr) {
        return nullptr;
    }
    return held->lanes.getptr(*address);
}

OpenVerdict StreamReaderBook::open(
    int p_peer,
    const StreamLane &p_lane,
    uint64_t p_request,
    uint64_t p_epoch,
    uint32_t p_schema,
    uint64_t &r_token,
    const StreamTenure &p_tenure
) {
    NETW_ZONE_NC("Stream open", colors::WIRE);
    Connection &live = connection(p_peer);
    const uint64_t address = p_lane.address();
    const ParkedOpen *parked = live.parks.getptr(address);
    if (parked != nullptr && parked->request <= p_request) {
        live.parks.erase(address);
    }
    Lane *held = live.lanes.getptr(address);
    if (held != nullptr && p_request < held->request) {
        r_token = 0;
        return OpenVerdict::SUPERSEDED;
    }
    if (held != nullptr && p_request == held->request) {
        r_token = held->token;
        return held->invalidated ? OpenVerdict::INVALIDATED
                                 : OpenVerdict::REPEATED;
    }
    const uint64_t token = next_stream_token();
    if (token == 0) {
        r_token = 0;
        return OpenVerdict::EXHAUSTED;
    }
    if (held != nullptr) {
        live.addresses.erase(held->token);
        live.lanes.erase(address);
    }
    Lane fresh;
    fresh.request = p_request;
    fresh.token = token;
    fresh.epoch = p_epoch;
    fresh.schema = p_schema;
    fresh.origin = p_tenure;
    fresh.tenure = p_tenure;
    live.lanes.insert(address, fresh);
    live.addresses[token] = address;
    r_token = token;
    return OpenVerdict::MINTED;
}

void StreamReaderBook::park(const ParkedOpen &p_open) {
    Connection &live = connection(p_open.peer);
    const uint64_t address = p_open.lane.address();
    ParkedOpen *held = live.parks.getptr(address);
    if (held != nullptr && held->request > p_open.request) {
        return;
    }
    if (held != nullptr && held->request == p_open.request) {
        held->tenure = p_open.tenure;
        held->anchor = p_open.anchor;
        held->anchor_author = p_open.anchor_author;
        return;
    }
    live.parks[address] = p_open;
}

LocalVector<ParkedOpen> StreamReaderBook::take_parks(int64_t p_route) {
    LocalVector<ParkedOpen> out;
    for (KeyValue<int, Connection> &each : connections) {
        LocalVector<uint64_t> taken;
        for (const KeyValue<uint64_t, ParkedOpen> &parked : each.value.parks) {
            if (StreamLane::route_of(parked.key) == p_route) {
                out.push_back(parked.value);
                taken.push_back(parked.key);
            }
        }
        for (uint32_t at = 0; at < taken.size(); ++at) {
            each.value.parks.erase(taken[at]);
        }
    }
    return out;
}

LocalVector<ParkedOpen> StreamReaderBook::expire_parks(
    int p_peer,
    int64_t p_now_ms,
    int64_t p_interval_ms
) {
    LocalVector<ParkedOpen> out;
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return out;
    }
    LocalVector<uint64_t> expired;
    for (const KeyValue<uint64_t, ParkedOpen> &parked : live->parks) {
        if (p_now_ms - parked.value.parked_at_ms >= p_interval_ms) {
            out.push_back(parked.value);
            expired.push_back(parked.key);
        }
    }
    for (uint32_t at = 0; at < expired.size(); ++at) {
        live->parks.erase(expired[at]);
    }
    expired_parks += out.size();
    return out;
}

LocalVector<int> StreamReaderBook::parked_peers() const {
    LocalVector<int> out;
    for (const KeyValue<int, Connection> &each : connections) {
        if (!each.value.parks.is_empty()) {
            out.push_back(each.key);
        }
    }
    return out;
}

LocalVector<BoundLane> StreamReaderBook::lanes_bound_before(
    int64_t p_route,
    uint64_t p_tenure
) const {
    LocalVector<BoundLane> out;
    for (const KeyValue<int, Connection> &each : connections) {
        for (const KeyValue<uint64_t, Lane> &lane : each.value.lanes) {
            if (StreamLane::route_of(lane.key) != p_route
                || !lane.value.tenure.bound
                || lane.value.tenure.tenure >= p_tenure) {
                continue;
            }
            BoundLane bound;
            bound.peer = each.key;
            bound.lane.route = p_route;
            bound.lane.ordinal = uint8_t((lane.key >> 8) & 0xFF);
            bound.lane.family = StreamFamily(uint8_t(lane.key & 0xFF));
            bound.tenure = lane.value.tenure;
            out.push_back(bound);
        }
    }
    return out;
}

bool StreamReaderBook::follow_tenure(
    int p_peer,
    const StreamLane &p_lane,
    uint64_t p_tenure,
    bool p_admitted
) {
    Connection *live = connections.getptr(p_peer);
    Lane *held = live != nullptr ? live->lanes.getptr(p_lane.address())
                                 : nullptr;
    if (held == nullptr || !held->tenure.bound
        || held->tenure.tenure > p_tenure) {
        return false;
    }
    held->tenure.tenure = p_tenure;
    held->floor = binding_floor(held->origin, held->tenure);
    held->held = !p_admitted;
    return true;
}

bool StreamReaderBook::admits_row(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    const Lane *held = lane_at(p_peer, p_token);
    if (held == nullptr) {
        return false;
    }
    if (held->held) {
        held_rows += 1;
        return false;
    }
    if (p_revision < held->floor) {
        fenced_rows += 1;
        return false;
    }
    return true;
}

bool StreamReaderBook::answers_stray(
    int p_peer,
    uint64_t p_token,
    int64_t p_now_ms,
    int64_t p_interval_ms
) {
    if (p_token == 0) {
        return false;
    }
    Connection &live = connection(p_peer);
    const int64_t *answered = live.strays.getptr(p_token);
    if (answered != nullptr && p_now_ms - *answered < p_interval_ms) {
        return false;
    }
    if (answered == nullptr && live.strays.size() >= STRAY_MEMORY) {
        live.strays.clear();
    }
    live.strays[p_token] = p_now_ms;
    return true;
}

bool StreamReaderBook::holds(int p_peer, const StreamLane &p_lane) const {
    const Connection *live = connections.getptr(p_peer);
    const Lane *held = live != nullptr ? live->lanes.getptr(p_lane.address())
                                       : nullptr;
    return held != nullptr && held->held;
}

uint32_t StreamReaderBook::close_anchors_before(
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_author
) {
    return close_where(
        p_route,
        &anchor_superseded,
        anchor_bar(p_anchor, p_author)
    );
}

uint32_t StreamReaderBook::close_where(
    int64_t p_route,
    TenureDooms p_dooms,
    const StreamTenure &p_bar
) {
    uint32_t closed = 0;
    for (KeyValue<int, Connection> &each : connections) {
        LocalVector<uint64_t> doomed;
        for (const KeyValue<uint64_t, Lane> &lane : each.value.lanes) {
            if (StreamLane::route_of(lane.key) == p_route
                && p_dooms(lane.value.tenure, p_bar)) {
                doomed.push_back(lane.key);
            }
        }
        for (uint32_t at = 0; at < doomed.size(); ++at) {
            Lane *held = each.value.lanes.getptr(doomed[at]);
            if (held != nullptr) {
                each.value.addresses.erase(held->token);
            }
            each.value.lanes.erase(doomed[at]);
        }
        closed += doomed.size();
    }
    return closed;
}

uint32_t StreamReaderBook::parked_count() const {
    uint32_t total = 0;
    for (const KeyValue<int, Connection> &each : connections) {
        total += uint32_t(each.value.parks.size());
    }
    return total;
}

bool StreamReaderBook::reaches(int p_peer, uint64_t p_token) {
    const Lane *held = lane_at(p_peer, p_token);
    if (held == nullptr || held->invalidated) {
        unknown_tokens += 1;
        return false;
    }
    return true;
}

bool StreamReaderBook::names(
    int p_peer,
    uint64_t p_token,
    StreamLane &r_lane
) const {
    const Connection *held = connections.getptr(p_peer);
    if (held == nullptr) {
        return false;
    }
    const uint64_t *address = held->addresses.getptr(p_token);
    if (address == nullptr) {
        return false;
    }
    r_lane.route = StreamLane::route_of(*address);
    r_lane.ordinal = uint8_t((*address >> 8) & 0xFF);
    r_lane.family = StreamFamily(uint8_t(*address & 0xFF));
    return true;
}

uint64_t StreamReaderBook::token_at(
    int p_peer,
    const StreamLane &p_lane
) const {
    const Connection *held = connections.getptr(p_peer);
    if (held == nullptr) {
        return 0;
    }
    const Lane *lane = held->lanes.getptr(p_lane.address());
    return lane == nullptr || lane->invalidated ? 0 : lane->token;
}

bool StreamReaderBook::invalidate(
    int p_peer,
    const StreamLane &p_lane,
    uint64_t &r_request,
    uint64_t &r_token
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return false;
    }
    Lane *held = live->lanes.getptr(p_lane.address());
    if (held == nullptr || held->invalidated) {
        return false;
    }
    held->invalidated = true;
    r_request = held->request;
    r_token = held->token;
    return true;
}

bool StreamReaderBook::close(int p_peer, uint64_t p_token) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return false;
    }
    const uint64_t *address = live->addresses.getptr(p_token);
    if (address == nullptr) {
        unknown_tokens += 1;
        return false;
    }
    const uint64_t held = *address;
    live->addresses.erase(p_token);
    live->lanes.erase(held);
    return true;
}

void StreamReaderBook::forget_peer(int p_peer) {
    connections.erase(p_peer);
}

void StreamReaderBook::close_route(int64_t p_route) {
    take_parks(p_route);
    for (KeyValue<int, Connection> &each : connections) {
        LocalVector<uint64_t> doomed;
        for (const KeyValue<uint64_t, Lane> &lane : each.value.lanes) {
            if (StreamLane::route_of(lane.key) == p_route) {
                doomed.push_back(lane.key);
            }
        }
        for (uint32_t at = 0; at < doomed.size(); ++at) {
            Lane *held = each.value.lanes.getptr(doomed[at]);
            if (held != nullptr) {
                each.value.addresses.erase(held->token);
            }
            each.value.lanes.erase(doomed[at]);
        }
    }
}

void StreamReaderBook::clear() {
    connections.clear();
}

uint32_t StreamReaderBook::stream_count() const {
    uint32_t total = 0;
    for (const KeyValue<int, Connection> &each : connections) {
        total += uint32_t(each.value.lanes.size());
    }
    return total;
}

} // namespace netw::wire
