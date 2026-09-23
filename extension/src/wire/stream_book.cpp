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

} // namespace

uint64_t next_stream_token() {
    if (token_counter == UINT64_MAX) {
        NETW_ERR_V(
            0,
            sys::WIRE,
            "This process has minted every stream token it can and cannot "
            "open another. A fresh transport connection is required."
        );
    }
    return ++token_counter;
}

uint64_t next_open_request() {
    if (request_counter == UINT64_MAX) {
        NETW_ERR_V(
            0,
            sys::WIRE,
            "This process has minted every stream request it can and cannot "
            "open another. A fresh transport connection is required."
        );
    }
    return ++request_counter;
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

uint32_t StreamReaderBook::close_tenures_before(
    int64_t p_route,
    uint64_t p_tenure
) {
    uint32_t closed = 0;
    for (KeyValue<int, Connection> &each : connections) {
        LocalVector<uint64_t> doomed;
        for (const KeyValue<uint64_t, Lane> &lane : each.value.lanes) {
            if (StreamLane::route_of(lane.key) == p_route
                && lane.value.tenure.bound
                && lane.value.tenure.tenure < p_tenure) {
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

SnapshotReceiver *StreamReaderBook::receiver(int p_peer, uint64_t p_token) {
    Lane *held = lane_at(p_peer, p_token);
    if (held == nullptr || held->invalidated) {
        unknown_tokens += 1;
        return nullptr;
    }
    return &held->receiver;
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
    held->receiver.reset();
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

StreamWriterBook::Lane *StreamWriterBook::lane_at(
    int p_peer,
    const StreamLane &p_lane
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return nullptr;
    }
    return live->lanes.getptr(p_lane.address());
}

StreamWriterBook::Lane *StreamWriterBook::lane_by_token(
    int p_peer,
    uint64_t p_token
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr || p_token == 0) {
        return nullptr;
    }
    const uint64_t *address = live->tokens.getptr(p_token);
    if (address == nullptr) {
        return nullptr;
    }
    Lane *held = live->lanes.getptr(*address);
    return held != nullptr && held->token == p_token ? held : nullptr;
}

uint64_t StreamWriterBook::open(
    int p_peer,
    const StreamLane &p_lane,
    uint64_t p_epoch,
    uint32_t p_schema,
    const StreamTenure &p_tenure
) {
    NETW_ZONE_NC("Stream request", colors::WIRE);
    Connection &live = connections[p_peer];
    const uint64_t address = p_lane.address();
    Lane *held = live.lanes.getptr(address);
    if (held != nullptr && !held->token_was_reset && held->epoch == p_epoch
        && held->schema == p_schema
        && held->tenure.tenure == p_tenure.tenure
        && held->tenure.bound == p_tenure.bound) {
        return held->request;
    }
    const uint64_t request = next_open_request();
    if (request == 0) {
        return 0;
    }
    if (held != nullptr) {
        live.requests.erase(held->request);
        live.tokens.erase(held->token);
        live.lanes.erase(address);
    }
    Lane fresh;
    fresh.request = request;
    fresh.epoch = p_epoch;
    fresh.schema = p_schema;
    fresh.tenure = p_tenure;
    live.lanes.insert(address, fresh);
    live.requests[request] = address;
    return request;
}

bool StreamWriterBook::holds_tenure(
    int p_peer,
    const StreamLane &p_lane,
    const StreamTenure &p_tenure
) {
    const Lane *held = lane_at(p_peer, p_lane);
    return held == nullptr
        || (held->tenure.tenure == p_tenure.tenure
            && held->tenure.bound == p_tenure.bound);
}

LocalVector<ClosedLane> StreamWriterBook::close_tenures_other_than(
    int64_t p_route,
    uint64_t p_tenure
) {
    LocalVector<ClosedLane> out;
    for (KeyValue<int, Connection> &each : connections) {
        LocalVector<uint64_t> doomed;
        for (const KeyValue<uint64_t, Lane> &lane : each.value.lanes) {
            if (StreamLane::route_of(lane.key) == p_route
                && lane.value.tenure.bound
                && lane.value.tenure.tenure != p_tenure) {
                doomed.push_back(lane.key);
            }
        }
        for (uint32_t at = 0; at < doomed.size(); ++at) {
            Lane *held = each.value.lanes.getptr(doomed[at]);
            if (held == nullptr) {
                continue;
            }
            if (held->token != 0) {
                ClosedLane closed;
                closed.peer = each.key;
                closed.token = held->token;
                out.push_back(closed);
            }
            each.value.requests.erase(held->request);
            each.value.tokens.erase(held->token);
            each.value.lanes.erase(doomed[at]);
        }
    }
    return out;
}

ReadyVerdict StreamWriterBook::ready(
    int p_peer,
    uint64_t p_request,
    uint64_t p_token
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return ReadyVerdict::UNKNOWN;
    }
    const uint64_t *address = live->requests.getptr(p_request);
    if (address == nullptr) {
        return ReadyVerdict::UNKNOWN;
    }
    Lane *held = live->lanes.getptr(*address);
    if (held == nullptr) {
        return ReadyVerdict::UNKNOWN;
    }
    live->tokens.erase(held->token);
    held->token = p_token;
    held->token_was_reset = false;
    live->tokens[p_token] = *address;
    return ReadyVerdict::SEATED;
}

bool StreamWriterBook::reset(
    int p_peer,
    uint64_t p_request,
    uint64_t p_token
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return false;
    }
    const uint64_t *address = live->requests.getptr(p_request);
    if (address == nullptr) {
        return false;
    }
    Lane *held = live->lanes.getptr(*address);
    if (held == nullptr) {
        return false;
    }
    if (held->token != 0 && held->token != p_token) {
        return false;
    }
    live->tokens.erase(held->token);
    held->token = 0;
    held->token_was_reset = true;
    held->sender.reset();
    return true;
}

uint64_t StreamWriterBook::token_of(int p_peer, const StreamLane &p_lane) {
    const Lane *held = lane_at(p_peer, p_lane);
    return held == nullptr ? 0 : held->token;
}

uint64_t StreamWriterBook::request_of(int p_peer, const StreamLane &p_lane) {
    const Lane *held = lane_at(p_peer, p_lane);
    return held == nullptr ? 0 : held->request;
}

SnapshotSender *StreamWriterBook::sender(
    int p_peer,
    const StreamLane &p_lane
) {
    Lane *held = lane_at(p_peer, p_lane);
    return held == nullptr ? nullptr : &held->sender;
}

SnapshotSender *StreamWriterBook::sender_of_token(
    int p_peer,
    uint64_t p_token
) {
    Lane *held = lane_by_token(p_peer, p_token);
    return held == nullptr ? nullptr : &held->sender;
}

bool StreamWriterBook::names(
    int p_peer,
    uint64_t p_token,
    StreamLane &r_lane
) const {
    const Connection *live = connections.getptr(p_peer);
    if (live == nullptr || p_token == 0) {
        return false;
    }
    const uint64_t *address = live->tokens.getptr(p_token);
    if (address == nullptr) {
        return false;
    }
    const Lane *held = live->lanes.getptr(*address);
    if (held == nullptr || held->token != p_token) {
        return false;
    }
    r_lane.route = StreamLane::route_of(*address);
    r_lane.ordinal = uint8_t((*address >> 8) & 0xFF);
    r_lane.family = StreamFamily(uint8_t(*address & 0xFF));
    return true;
}

ReceiptVerdict StreamWriterBook::receipt(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    Lane *held = lane_by_token(p_peer, p_token);
    if (held == nullptr) {
        unknown_receipts += 1;
        return ReceiptVerdict::IGNORED;
    }
    return held->sender.receipt(p_revision);
}

bool StreamWriterBook::repair_due(
    int p_peer,
    const StreamLane &p_lane,
    int64_t p_now_ms,
    int64_t p_smoothed_rtt_ms
) {
    Lane *held = lane_at(p_peer, p_lane);
    if (held == nullptr || held->sender.quiet()) {
        return false;
    }
    const int64_t wait = repair_interval_ms(p_smoothed_rtt_ms);
    if (p_now_ms - held->repaired_at_ms < wait) {
        return false;
    }
    held->repaired_at_ms = p_now_ms;
    return true;
}

void StreamWriterBook::note_attempt(
    int p_peer,
    const StreamLane &p_lane,
    int64_t p_now_ms
) {
    Lane *held = lane_at(p_peer, p_lane);
    if (held != nullptr) {
        held->repaired_at_ms = p_now_ms;
    }
}

LocalVector<StreamLane> StreamWriterBook::unready(int p_peer) const {
    LocalVector<StreamLane> out;
    const Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return out;
    }
    for (const KeyValue<uint64_t, Lane> &each : live->lanes) {
        if (each.value.token != 0) {
            continue;
        }
        StreamLane lane;
        lane.route = StreamLane::route_of(each.key);
        lane.ordinal = uint8_t((each.key >> 8) & 0xFF);
        lane.family = StreamFamily(uint8_t(each.key & 0xFF));
        out.push_back(lane);
    }
    return out;
}

bool StreamWriterBook::close(
    int p_peer,
    const StreamLane &p_lane,
    uint64_t &r_token
) {
    Connection *live = connections.getptr(p_peer);
    if (live == nullptr) {
        return false;
    }
    const uint64_t address = p_lane.address();
    Lane *held = live->lanes.getptr(address);
    if (held == nullptr) {
        return false;
    }
    r_token = held->token;
    live->requests.erase(held->request);
    live->tokens.erase(held->token);
    live->lanes.erase(address);
    return r_token != 0;
}

void StreamWriterBook::forget_peer(int p_peer) {
    connections.erase(p_peer);
}

void StreamWriterBook::close_route(int64_t p_route) {
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
                each.value.requests.erase(held->request);
                each.value.tokens.erase(held->token);
            }
            each.value.lanes.erase(doomed[at]);
        }
    }
}

void StreamWriterBook::clear() {
    connections.clear();
}

uint32_t StreamWriterBook::stream_count() const {
    uint32_t total = 0;
    for (const KeyValue<int, Connection> &each : connections) {
        total += uint32_t(each.value.lanes.size());
    }
    return total;
}

} // namespace netw::wire
