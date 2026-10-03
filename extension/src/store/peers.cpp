#include "netw/store/world.hpp"

#include "netw/wire/snapshot_stream.hpp"

using namespace godot;

namespace netw::store {

namespace {

void size_rows(
    PeerRows &r_peer,
    uint32_t p_count,
    uint32_t p_words,
    uint32_t p_carried
) {
    r_peer.admitted.resize(p_count);
    r_peer.sendable.resize(p_count);
    r_peer.owed.resize(p_count);
    r_peer.queued.resize(p_count);
    r_peer.due.resize(p_count);
    r_peer.stream.resize(p_count);
    r_peer.request.resize(p_count);
    r_peer.token.resize(p_count);
    r_peer.origin.resize(p_count);
    r_peer.opened.resize(p_count);
    r_peer.next_rev.resize(p_count);
    r_peer.acked_rev.resize(p_count);
    r_peer.exposed_rev.resize(p_count);
    r_peer.baseline.resize(p_count * p_words);
    r_peer.baseline_carried.resize(p_count * p_carried);
    r_peer.acc.resize(p_count);
    r_peer.last_sent.resize(p_count);
    r_peer.refresh_at.resize(p_count);
    r_peer.repair_at.resize(p_count);
}

constexpr uint32_t SPARE_IMAGES = 256;

template <typename T>
void append_moved(LocalVector<T> &r_into, T &r_value) {
    const uint32_t at = r_into.size();
    r_into.resize(at + 1);
    r_into[at] = std::move(r_value);
}

void spare(LocalVector<wire::CodeRow> &r_spare, Flight &r_flight) {
    if (r_spare.size() < SPARE_IMAGES) {
        r_flight.image.clear_values();
        append_moved(r_spare, r_flight.image);
    }
}

void evict(
    LocalVector<Flight> &r_flights,
    LocalVector<wire::CodeRow> &r_spare
) {
    while (r_flights.size() > wire::SNAPSHOT_STAGE_DEPTH) {
        uint32_t oldest = r_flights.size();
        for (uint32_t at = 0; at < r_flights.size(); ++at) {
            if (!r_flights[at].pinned) {
                oldest = at;
                break;
            }
        }
        if (oldest == r_flights.size()) {
            return;
        }
        spare(r_spare, r_flights[oldest]);
        r_flights.remove_at(oldest);
    }
}

int32_t flight_at(const LocalVector<Flight> &p_flights, uint64_t p_revision) {
    for (uint32_t at = 0; at < p_flights.size(); ++at) {
        if (p_flights[at].revision == p_revision) {
            return int32_t(at);
        }
    }
    return -1;
}

wire::StreamLane lane_of(const Table &p_table, uint32_t p_row) {
    wire::StreamLane lane;
    lane.route = p_table.routes[p_row];
    lane.ordinal = p_table.ordinals[p_row];
    lane.family = wire::StreamFamily(uint8_t(p_table.family));
    return lane;
}

bool tenure_other_than(
    const wire::StreamTenure &p_lane,
    const wire::StreamTenure &p_bar
) {
    return p_lane.bound && p_lane.tenure != p_bar.tenure;
}

bool anchor_other_than(
    const wire::StreamTenure &p_lane,
    const wire::StreamTenure &p_bar
) {
    return p_lane.anchor != p_bar.anchor
        || p_lane.anchor_author != p_bar.anchor_author;
}

void wake(PeerSide &r_side, uint64_t p_lane) {
    for (uint32_t at = 0; at < r_side.dormant.size(); ++at) {
        if (r_side.dormant[at] == p_lane) {
            r_side.dormant.remove_at(at);
            return;
        }
    }
}

bool refresh_later(const Refresh &p_left, const Refresh &p_right) {
    return p_left.at > p_right.at;
}

void sift_up(LocalVector<Refresh> &r_heap, uint32_t p_at) {
    while (p_at > 0) {
        const uint32_t parent = (p_at - 1) / 2;
        if (!refresh_later(r_heap[parent], r_heap[p_at])) {
            return;
        }
        const Refresh held = r_heap[parent];
        r_heap[parent] = r_heap[p_at];
        r_heap[p_at] = held;
        p_at = parent;
    }
}

void sift_down(LocalVector<Refresh> &r_heap, uint32_t p_at) {
    const uint32_t count = r_heap.size();
    while (true) {
        const uint32_t left = p_at * 2 + 1;
        const uint32_t right = left + 1;
        uint32_t least = p_at;
        if (left < count && refresh_later(r_heap[least], r_heap[left])) {
            least = left;
        }
        if (right < count && refresh_later(r_heap[least], r_heap[right])) {
            least = right;
        }
        if (least == p_at) {
            return;
        }
        const Refresh held = r_heap[least];
        r_heap[least] = r_heap[p_at];
        r_heap[p_at] = held;
        p_at = least;
    }
}

} // namespace

void Bits::resize(uint32_t p_count) {
    const uint32_t needed = (p_count + 63) / 64;
    const uint32_t had = words.size();
    words.resize(needed);
    for (uint32_t at = had; at < needed; ++at) {
        words[at] = 0;
    }
    if ((p_count & 63) != 0) {
        words[needed - 1] &= (uint64_t(1) << (p_count & 63)) - 1;
    }
    count = p_count;
}

void Bits::clear() {
    for (uint32_t at = 0; at < words.size(); ++at) {
        words[at] = 0;
    }
}

void Bits::fill() {
    for (uint32_t at = 0; at < words.size(); ++at) {
        words[at] = ~uint64_t(0);
    }
    if ((count & 63) != 0 && !words.is_empty()) {
        words[words.size() - 1] &= (uint64_t(1) << (count & 63)) - 1;
    }
}

uint32_t Bits::population() const {
    uint32_t total = 0;
    for (uint32_t at = 0; at < words.size(); ++at) {
        uint64_t word = words[at];
        while (word != 0) {
            word &= word - 1;
            total += 1;
        }
    }
    return total;
}

void Table::fresh_peer_row(PeerRows &r_peer, uint32_t p_row) {
    r_peer.admitted.set(p_row, false);
    r_peer.sendable.set(p_row, false);
    r_peer.owed.set(p_row, true);
    r_peer.queued.set(p_row, false);
    r_peer.due.set(p_row, false);
    r_peer.stream[p_row] = uint8_t(Stream::NONE);
    r_peer.request[p_row] = 0;
    r_peer.token[p_row] = 0;
    r_peer.origin[p_row] = wire::StreamTenure();
    r_peer.opened[p_row] = wire::StreamTenure();
    r_peer.next_rev[p_row] = wire::SNAPSHOT_FIRST_REVISION;
    r_peer.acked_rev[p_row] = 0;
    r_peer.exposed_rev[p_row] = 0;
    for (uint32_t at = 0; at < words; ++at) {
        r_peer.baseline[p_row * words + at] = 0;
    }
    for (uint32_t at = 0; at < carried; ++at) {
        r_peer.baseline_carried[p_row * carried + at] = Variant();
    }
    r_peer.acc[p_row] = 0.0f;
    r_peer.last_sent[p_row] = -1;
    r_peer.refresh_at[p_row] = -1;
    r_peer.repair_at[p_row] = 0;
}

void Table::push_peer_rows() {
    const uint32_t count = slots.size();
    for (uint32_t peer = 0; peer < peers.size(); ++peer) {
        size_rows(peers[peer], count, words, carried);
        fresh_peer_row(peers[peer], count - 1);
    }
}

void Table::move_peer_rows(uint32_t p_to, uint32_t p_from) {
    for (uint32_t peer = 0; peer < peers.size(); ++peer) {
        PeerRows &held = peers[peer];
        held.admitted.set(p_to, held.admitted.get(p_from));
        held.sendable.set(p_to, held.sendable.get(p_from));
        held.owed.set(p_to, held.owed.get(p_from));
        held.queued.set(p_to, held.queued.get(p_from));
        held.due.set(p_to, held.due.get(p_from));
        held.stream[p_to] = held.stream[p_from];
        held.request[p_to] = held.request[p_from];
        held.token[p_to] = held.token[p_from];
        held.origin[p_to] = held.origin[p_from];
        held.opened[p_to] = held.opened[p_from];
        held.next_rev[p_to] = held.next_rev[p_from];
        held.acked_rev[p_to] = held.acked_rev[p_from];
        held.exposed_rev[p_to] = held.exposed_rev[p_from];
        for (uint32_t at = 0; at < words; ++at) {
            held.baseline[p_to * words + at]
                = held.baseline[p_from * words + at];
        }
        for (uint32_t at = 0; at < carried; ++at) {
            held.baseline_carried[p_to * carried + at]
                = held.baseline_carried[p_from * carried + at];
        }
        held.acc[p_to] = held.acc[p_from];
        held.last_sent[p_to] = held.last_sent[p_from];
        held.refresh_at[p_to] = held.refresh_at[p_from];
        held.repair_at[p_to] = held.repair_at[p_from];
    }
}

void Table::shrink_peer_rows(uint32_t p_count) {
    for (uint32_t peer = 0; peer < peers.size(); ++peer) {
        size_rows(peers[peer], p_count, words, carried);
    }
}

void Table::size_peers(uint32_t p_count) {
    const uint32_t had = peers.size();
    if (p_count <= had) {
        return;
    }
    peers.resize(p_count);
    for (uint32_t peer = had; peer < p_count; ++peer) {
        size_rows(peers[peer], slots.size(), words, carried);
        for (uint32_t row = 0; row < slots.size(); ++row) {
            fresh_peer_row(peers[peer], row);
        }
    }
}

void Table::open_peer(uint32_t p_peer) {
    size_peers(p_peer + 1);
    for (uint32_t row = 0; row < slots.size(); ++row) {
        fresh_peer_row(peers[p_peer], row);
    }
}

void Table::reset_stream(uint32_t p_row, uint32_t p_peer, Stream p_stream) {
    PeerRows &held = peers[p_peer];
    held.stream[p_row] = uint8_t(p_stream);
    held.request[p_row] = 0;
    held.token[p_row] = 0;
    held.origin[p_row] = wire::StreamTenure();
    held.opened[p_row] = wire::StreamTenure();
    held.next_rev[p_row] = wire::SNAPSHOT_FIRST_REVISION;
    held.acked_rev[p_row] = 0;
    held.exposed_rev[p_row] = 0;
    for (uint32_t at = 0; at < words; ++at) {
        held.baseline[p_row * words + at] = 0;
    }
    for (uint32_t at = 0; at < carried; ++at) {
        held.baseline_carried[p_row * carried + at] = Variant();
    }
    held.last_sent[p_row] = -1;
    held.refresh_at[p_row] = -1;
    held.repair_at[p_row] = 0;
    refresh_owed(p_row, p_peer);
}

bool Table::differs(uint32_t p_row, uint32_t p_peer) const {
    const PeerRows &held = peers[p_peer];
    if (held.acked_rev[p_row] == 0) {
        return true;
    }
    const uint64_t *mine = codes_of(p_row);
    const uint64_t *theirs = held.baseline.ptr() + uint64_t(p_row) * words;
    for (uint32_t at = 0; at < words; ++at) {
        if (mine[at] != theirs[at]) {
            return true;
        }
    }
    for (uint32_t at = 0; at < carried; ++at) {
        if (carried_values[p_row * carried + at]
            != held.baseline_carried[p_row * carried + at]) {
            return true;
        }
    }
    return false;
}

bool Table::owes(uint32_t p_row, uint32_t p_peer) const {
    const PeerRows &held = peers[p_peer];
    return held.exposed_rev[p_row] > held.acked_rev[p_row]
        || differs(p_row, p_peer);
}

void Table::refresh_owed(uint32_t p_row, uint32_t p_peer) {
    peers[p_peer].owed.set(p_row, owes(p_row, p_peer));
}

void Table::refresh_sendable(uint32_t p_row, uint32_t p_peer) {
    PeerRows &held = peers[p_peer];
    held.sendable.set(
        p_row,
        held.admitted.get(p_row) && sources[p_row] == uint8_t(Source::LOCAL)
    );
}

void Table::promote(
    uint32_t p_row,
    uint32_t p_peer,
    uint64_t p_revision,
    const wire::CodeRow &p_image
) {
    PeerRows &held = peers[p_peer];
    const uint32_t given
        = p_image.word_count() < words ? p_image.word_count() : words;
    const uint64_t *image = p_image.word_data();
    for (uint32_t at = 0; at < words; ++at) {
        held.baseline[p_row * words + at] = at < given ? image[at] : 0;
    }
    const Variant *values = p_image.value_data();
    for (uint32_t at = 0; at < carried; ++at) {
        held.baseline_carried[p_row * carried + at]
            = values != nullptr ? values[at] : Variant();
    }
    held.acked_rev[p_row] = p_revision;
    refresh_owed(p_row, p_peer);
}

bool Table::holds_image(uint32_t p_row, const wire::CodeRow &p_image) const {
    const uint32_t given
        = p_image.word_count() < words ? p_image.word_count() : words;
    const uint64_t *image = p_image.word_data();
    const uint64_t *mine = codes_of(p_row);
    for (uint32_t at = 0; at < words; ++at) {
        if (mine[at] != (at < given ? image[at] : 0)) {
            return false;
        }
    }
    const Variant *values = p_image.value_data();
    for (uint32_t at = 0; at < carried; ++at) {
        if (carried_values[p_row * carried + at]
            != (values != nullptr ? values[at] : Variant())) {
            return false;
        }
    }
    return true;
}

uint64_t World::lane_key(const Table &p_table, uint32_t p_row) const {
    return lane_of(p_table, p_row).address();
}

void World::forget_lane(const RowAddress &p_at) {
    Table &held = *tables[p_at.table];
    if ((held.flags[p_at.row] & Table::DECLARED) == 0) {
        return;
    }
    const uint64_t key = lane_key(held, p_at.row);
    const RowAddress *named = lane_rows.getptr(key);
    if (named != nullptr && named->table == p_at.table
        && named->row == p_at.row) {
        lane_rows.erase(key);
    }
    for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
        held.peers[peer].queued.set(p_at.row, false);
        held.peers[peer].due.set(p_at.row, false);
    }
    held.flags[p_at.row] &= ~uint8_t(Table::DECLARED);
}

void World::name_lane(const RowAddress &p_at, uint8_t p_ordinal) {
    Table &held = *tables[p_at.table];
    const bool named = (held.flags[p_at.row] & Table::DECLARED) != 0;
    if (named && held.ordinals[p_at.row] == p_ordinal) {
        return;
    }
    forget_lane(p_at);
    if (held.routes[p_at.row] <= 0) {
        return;
    }
    held.ordinals[p_at.row] = p_ordinal;
    held.flags[p_at.row] |= Table::DECLARED;
    lane_rows[lane_key(held, p_at.row)] = p_at;
}

RowAddress World::lane_row(const wire::StreamLane &p_lane) const {
    return lane_row(p_lane.address());
}

RowAddress World::lane_row(uint64_t p_address) const {
    const RowAddress *named = lane_rows.getptr(p_address);
    return named != nullptr ? *named : RowAddress();
}

uint32_t World::peer_join(int p_id) {
    const uint32_t *held = peer_index.getptr(p_id);
    if (held != nullptr) {
        return *held;
    }
    uint32_t index = 0;
    if (!free_peers.is_empty()) {
        index = free_peers[free_peers.size() - 1];
        free_peers.resize(free_peers.size() - 1);
    } else {
        index = sides.size();
        sides.resize(index + 1);
    }
    PeerSide &side = sides[index];
    side.id = p_id;
    side.joined = true;
    side.reachable = false;
    forget_side(side);
    side.visible.resize(slot_route.size());
    side.visible.clear();
    side.held.resize(slot_route.size());
    side.held.clear();
    for (uint32_t at = 0; at < tables.size(); ++at) {
        tables[at]->open_peer(index);
    }
    peer_index[p_id] = index;
    return index;
}

void World::forget_side(PeerSide &r_side) {
    r_side.flights.clear();
    r_side.requests.clear();
    r_side.tokens.clear();
    r_side.refreshes.clear();
    r_side.due.clear();
    r_side.dormant.clear();
}

void World::peer_leave(int p_id) {
    const uint32_t *held = peer_index.getptr(p_id);
    if (held == nullptr) {
        return;
    }
    const uint32_t index = *held;
    peer_index.erase(p_id);
    for (uint32_t at = 0; at < tables.size(); ++at) {
        tables[at]->open_peer(index);
    }
    PeerSide &side = sides[index];
    side.id = 0;
    side.joined = false;
    side.reachable = false;
    side.flights = HashMap<uint64_t, LocalVector<Flight>>();
    side.requests = HashMap<uint64_t, uint64_t>();
    side.tokens = HashMap<uint64_t, uint64_t>();
    side.refreshes.reset();
    side.due.reset();
    side.dormant.reset();
    side.visible.clear();
    side.held.clear();
    free_peers.push_back(index);
    trim_departed();
}

void World::trim_departed() {
    while (!sides.is_empty() && !sides[sides.size() - 1].joined) {
        const uint32_t last = sides.size() - 1;
        for (uint32_t at = 0; at < tables.size(); ++at) {
            tables[at]->trim_peers(last);
        }
        for (uint32_t at = 0; at < free_peers.size(); ++at) {
            if (free_peers[at] == last) {
                free_peers.remove_at_unordered(at);
                break;
            }
        }
        sides.resize(last);
    }
}

void World::clear_peers() {
    for (uint32_t index = 0; index < sides.size(); ++index) {
        if (sides[index].joined) {
            peer_leave(sides[index].id);
        }
    }
}

uint32_t World::peer_of(int p_id) const {
    const uint32_t *held = peer_index.getptr(p_id);
    return held != nullptr ? *held : Table::NO_SLOT;
}

uint32_t World::flight_count(uint32_t p_peer) const {
    uint32_t total = 0;
    if (p_peer >= sides.size()) {
        return total;
    }
    for (const KeyValue<uint64_t, LocalVector<Flight>> &each :
         sides[p_peer].flights) {
        total += each.value.size();
    }
    return total;
}

void World::admit(const RowAddress &p_at, uint32_t p_peer, bool p_admitted) {
    Table &held = *tables[p_at.table];
    PeerRows &peer = held.peers[p_peer];
    if (peer.admitted.get(p_at.row) == p_admitted) {
        return;
    }
    peer.admitted.set(p_at.row, p_admitted);
    held.refresh_sendable(p_at.row, p_peer);
    held.refresh_owed(p_at.row, p_peer);
}

void World::hide(const RowAddress &p_at, uint32_t p_peer) {
    Table &held = *tables[p_at.table];
    held.peers[p_peer].admitted.set(p_at.row, false);
    held.refresh_sendable(p_at.row, p_peer);
    close_row(p_at, p_peer, Stream::NONE);
}

void World::set_source(const RowAddress &p_at, Source p_source) {
    Table &held = *tables[p_at.table];
    const uint8_t was = held.sources[p_at.row];
    if (was == uint8_t(p_source)) {
        return;
    }
    held.sources[p_at.row] = uint8_t(p_source);
    const bool reads = p_source != Source::NONE;
    if (reads != (was != uint8_t(Source::NONE))) {
        count_probe(held, p_at.row, reads);
    }
    for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
        held.refresh_sendable(p_at.row, peer);
    }
}

void World::note_changed(const RowAddress &p_at) {
    owe_changed(*tables[p_at.table], p_at.row);
}

void World::owe_changed(Table &p_table, uint32_t p_row) {
    Table &held = p_table;
    for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
        if (!peer_joined(peer)) {
            continue;
        }
        held.refresh_owed(p_row, peer);
        const uint64_t token = held.peers[peer].token[p_row];
        LocalVector<Flight> *flights
            = token != 0 ? sides[peer].flights.getptr(token) : nullptr;
        if (flights == nullptr) {
            continue;
        }
        bool unpinned = false;
        for (uint32_t at = 0; at < flights->size(); ++at) {
            unpinned = unpinned || (*flights)[at].pinned;
            (*flights)[at].pinned = false;
        }
        if (unpinned) {
            evict(*flights, spare_images);
        }
    }
}

void World::close_row(
    const RowAddress &p_at,
    uint32_t p_peer,
    Stream p_stream
) {
    Table &held = *tables[p_at.table];
    PeerRows &peer = held.peers[p_peer];
    PeerSide &side = sides[p_peer];
    if (peer.stream[p_at.row] == uint8_t(Stream::DORMANT)) {
        wake(side, lane_key(held, p_at.row));
    }
    const uint64_t token = peer.token[p_at.row];
    if (token != 0) {
        side.flights.erase(token);
        side.tokens.erase(token);
    }
    if (peer.request[p_at.row] != 0) {
        side.requests.erase(peer.request[p_at.row]);
    }
    held.reset_stream(p_at.row, p_peer, p_stream);
}

bool World::stream_row(
    int p_peer,
    const wire::StreamLane &p_lane,
    uint32_t &r_peer,
    RowAddress &r_at
) const {
    r_at = lane_row(p_lane);
    r_peer = peer_of(p_peer);
    return r_at.valid() && r_peer != Table::NO_SLOT;
}

uint64_t World::stream_open(
    int p_peer,
    const wire::StreamLane &p_lane,
    const wire::StreamTenure &p_tenure
) {
    const RowAddress at = lane_row(p_lane);
    if (!at.valid()) {
        return 0;
    }
    const uint32_t peer = peer_join(p_peer);
    PeerRows &held = tables[at.table]->peers[peer];
    if (held.stream[at.row] != uint8_t(Stream::NONE)
        && held.request[at.row] != 0 && held.opened[at.row] == p_tenure) {
        return held.request[at.row];
    }
    const uint64_t request = wire::next_open_request();
    if (request == 0) {
        return 0;
    }
    close_row(at, peer, Stream::OPENING);
    held.request[at.row] = request;
    held.origin[at.row] = p_tenure;
    held.opened[at.row] = p_tenure;
    sides[peer].requests[request] = p_lane.address();
    return request;
}

wire::ReadyVerdict World::stream_ready(
    int p_peer,
    uint64_t p_request,
    uint64_t p_token
) {
    const uint32_t peer = peer_of(p_peer);
    if (peer == Table::NO_SLOT || p_request == 0) {
        return wire::ReadyVerdict::UNKNOWN;
    }
    PeerSide &side = sides[peer];
    const uint64_t *address = side.requests.getptr(p_request);
    if (address == nullptr) {
        return wire::ReadyVerdict::UNKNOWN;
    }
    const uint64_t lane = *address;
    const RowAddress at = lane_row(lane);
    if (!at.valid()) {
        return wire::ReadyVerdict::UNKNOWN;
    }
    PeerRows &held = tables[at.table]->peers[peer];
    if (held.request[at.row] != p_request) {
        return wire::ReadyVerdict::UNKNOWN;
    }
    const uint64_t was = held.token[at.row];
    if (was != 0 && was != p_token) {
        LocalVector<Flight> *flights = side.flights.getptr(was);
        if (flights != nullptr) {
            LocalVector<Flight> carried;
            carried = *flights;
            side.flights.erase(was);
            side.flights[p_token] = carried;
        }
        side.tokens.erase(was);
    }
    held.stream[at.row] = uint8_t(Stream::READY);
    held.token[at.row] = p_token;
    side.tokens[p_token] = lane;
    return wire::ReadyVerdict::SEATED;
}

bool World::stream_reset(int p_peer, uint64_t p_request, uint64_t p_token) {
    const uint32_t peer = peer_of(p_peer);
    if (peer == Table::NO_SLOT) {
        return false;
    }
    if (p_request == 0) {
        const uint64_t *named
            = p_token != 0 ? sides[peer].tokens.getptr(p_token) : nullptr;
        const RowAddress at
            = named != nullptr ? lane_row(*named) : RowAddress();
        if (!at.valid()
            || tables[at.table]->peers[peer].token[at.row] != p_token) {
            return false;
        }
        close_row(at, peer, Stream::NONE);
        return true;
    }
    const uint64_t *address = sides[peer].requests.getptr(p_request);
    const RowAddress at
        = address != nullptr ? lane_row(*address) : RowAddress();
    if (!at.valid()) {
        return false;
    }
    const PeerRows &held = tables[at.table]->peers[peer];
    if (held.request[at.row] != p_request) {
        return false;
    }
    if (held.token[at.row] != 0 && held.token[at.row] != p_token) {
        return false;
    }
    close_row(at, peer, Stream::NONE);
    return true;
}

bool World::stream_close(
    int p_peer,
    const wire::StreamLane &p_lane,
    uint64_t &r_token
) {
    r_token = 0;
    uint32_t peer = 0;
    RowAddress at;
    if (!stream_row(p_peer, p_lane, peer, at)) {
        return false;
    }
    const PeerRows &held = tables[at.table]->peers[peer];
    if (held.stream[at.row] == uint8_t(Stream::NONE)) {
        return false;
    }
    r_token = held.token[at.row];
    close_row(at, peer, Stream::NONE);
    return true;
}

bool World::stream_hide(int p_peer, const wire::StreamLane &p_lane) {
    uint32_t peer = 0;
    RowAddress at;
    if (!stream_row(p_peer, p_lane, peer, at)) {
        return false;
    }
    const bool held
        = tables[at.table]->peers[peer].stream[at.row] != uint8_t(Stream::NONE);
    hide(at, peer);
    return held;
}

uint64_t World::token_of(int p_peer, const wire::StreamLane &p_lane) const {
    uint32_t peer = 0;
    RowAddress at;
    if (!stream_row(p_peer, p_lane, peer, at)) {
        return 0;
    }
    const PeerRows &held = tables[at.table]->peers[peer];
    return held.stream[at.row] == uint8_t(Stream::READY) ? held.token[at.row]
                                                         : 0;
}

uint64_t World::request_of(int p_peer, const wire::StreamLane &p_lane) const {
    uint32_t peer = 0;
    RowAddress at;
    if (!stream_row(p_peer, p_lane, peer, at)) {
        return 0;
    }
    return tables[at.table]->peers[peer].request[at.row];
}

bool World::names_request(
    int p_peer,
    uint64_t p_request,
    wire::StreamLane &r_lane
) const {
    const uint32_t peer = peer_of(p_peer);
    if (peer == Table::NO_SLOT) {
        return false;
    }
    const uint64_t *address = sides[peer].requests.getptr(p_request);
    const RowAddress at
        = address != nullptr ? lane_row(*address) : RowAddress();
    if (!at.valid()) {
        return false;
    }
    r_lane = lane_of(*tables[at.table], at.row);
    return true;
}

LocalVector<uint64_t> World::opening_requests(int p_peer) const {
    LocalVector<uint64_t> out;
    const uint32_t peer = peer_of(p_peer);
    if (peer == Table::NO_SLOT) {
        return out;
    }
    for (const KeyValue<uint64_t, uint64_t> &each : sides[peer].requests) {
        const RowAddress at = lane_row(each.value);
        if (at.valid()
            && tables[at.table]->peers[peer].stream[at.row]
                == uint8_t(Stream::OPENING)) {
            out.push_back(each.key);
        }
    }
    return out;
}

LocalVector<wire::ClosedLane> World::close_where(
    int64_t p_route,
    bool (*p_dooms)(const wire::StreamTenure &, const wire::StreamTenure &),
    const wire::StreamTenure &p_bar
) {
    LocalVector<wire::ClosedLane> out;
    for (uint32_t table = 0; table < tables.size(); ++table) {
        Table &held = *tables[table];
        for (uint32_t row = 0; row < held.row_count(); ++row) {
            if (held.routes[row] != p_route) {
                continue;
            }
            for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
                const PeerRows &rows = held.peers[peer];
                if (!peer_joined(peer)
                    || rows.stream[row] == uint8_t(Stream::NONE)
                    || !p_dooms(rows.opened[row], p_bar)) {
                    continue;
                }
                wire::ClosedLane closed;
                closed.peer = sides[peer].id;
                closed.token = rows.token[row];
                closed.lane = lane_of(held, row);
                out.push_back(closed);
                close_row({table, row}, peer, Stream::NONE);
            }
        }
    }
    return out;
}

LocalVector<wire::ClosedLane> World::park_tenures_other_than(
    int64_t p_route,
    uint64_t p_tenure
) {
    LocalVector<wire::ClosedLane> out;
    wire::StreamTenure bar;
    bar.tenure = p_tenure;
    for (uint32_t table = 0; table < tables.size(); ++table) {
        Table &held = *tables[table];
        for (uint32_t row = 0; row < held.row_count(); ++row) {
            if (held.routes[row] != p_route) {
                continue;
            }
            for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
                PeerRows &rows = held.peers[peer];
                if (!peer_joined(peer)
                    || !tenure_other_than(rows.opened[row], bar)) {
                    continue;
                }
                if (rows.stream[row] == uint8_t(Stream::OPENING)) {
                    close_row({table, row}, peer, Stream::NONE);
                    continue;
                }
                if (rows.stream[row] != uint8_t(Stream::READY)) {
                    continue;
                }
                rows.stream[row] = uint8_t(Stream::DORMANT);
                sides[peer].dormant.push_back(lane_key(held, row));
                if (sides[peer].dormant.size() > wire::DORMANT_LANE_CAP) {
                    evict_dormant(peer, out);
                }
            }
        }
    }
    return out;
}

void World::evict_dormant(
    uint32_t p_peer,
    LocalVector<wire::ClosedLane> &r_closed
) {
    const RowAddress at = lane_row(sides[p_peer].dormant[0]);
    if (!at.valid()) {
        sides[p_peer].dormant.remove_at(0);
        return;
    }
    wire::ClosedLane closed;
    closed.peer = sides[p_peer].id;
    closed.token = tables[at.table]->peers[p_peer].token[at.row];
    closed.lane = lane_of(*tables[at.table], at.row);
    r_closed.push_back(closed);
    close_row(at, p_peer, Stream::NONE);
}

bool World::stream_resume(
    uint32_t p_peer,
    const RowAddress &p_at,
    const wire::StreamTenure &p_bound
) {
    Table &table = *tables[p_at.table];
    PeerRows &held = table.peers[p_peer];
    const uint8_t stream = held.stream[p_at.row];
    if ((stream != uint8_t(Stream::READY)
         && stream != uint8_t(Stream::DORMANT))
        || !wire::binding_follows(held.origin[p_at.row], p_bound)
        || p_bound.tenure < held.opened[p_at.row].tenure) {
        return false;
    }
    const uint64_t floor = wire::binding_floor(held.origin[p_at.row], p_bound);
    if (held.next_rev[p_at.row] > floor && floor != 0
        && !(held.opened[p_at.row] == p_bound)) {
        return false;
    }
    PeerSide &side = sides[p_peer];
    if (stream == uint8_t(Stream::DORMANT)) {
        wake(side, lane_key(table, p_at.row));
    }
    if (!(held.opened[p_at.row] == p_bound)) {
        LocalVector<Flight> *flights
            = side.flights.getptr(held.token[p_at.row]);
        if (flights != nullptr) {
            for (uint32_t at = 0; at < flights->size(); ++at) {
                spare(spare_images, (*flights)[at]);
            }
            flights->clear();
        }
        held.next_rev[p_at.row] = floor > held.next_rev[p_at.row]
            ? floor
            : held.next_rev[p_at.row];
        held.opened[p_at.row] = p_bound;
    }
    held.stream[p_at.row] = uint8_t(Stream::READY);
    return true;
}

uint32_t World::dormant_count(int p_peer) const {
    const uint32_t peer = peer_of(p_peer);
    return peer != Table::NO_SLOT ? sides[peer].dormant.size() : 0;
}

LocalVector<wire::ClosedLane> World::close_anchors_other_than(
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_author
) {
    wire::StreamTenure bar;
    bar.anchor = p_anchor;
    bar.anchor_author = p_author;
    return close_where(p_route, &anchor_other_than, bar);
}

void World::route_closed(int64_t p_route) {
    for (uint32_t table = 0; table < tables.size(); ++table) {
        Table &held = *tables[table];
        for (uint32_t row = 0; row < held.row_count(); ++row) {
            if (held.routes[row] != p_route) {
                continue;
            }
            for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
                if (peer_joined(peer)) {
                    close_row({table, row}, peer, Stream::NONE);
                }
            }
        }
    }
}

void World::close_life(const RowAddress &p_at) {
    Table &held = *tables[p_at.table];
    for (uint32_t peer = 0; peer < held.peers.size(); ++peer) {
        if (peer_joined(peer)
            && held.peers[peer].stream[p_at.row] != uint8_t(Stream::NONE)) {
            close_row(p_at, peer, Stream::NONE);
        }
    }
}

uint64_t World::mint(
    const RowAddress &p_at,
    uint32_t p_peer,
    const wire::CodeRow &p_image,
    bool p_pinned
) {
    Table &table = *tables[p_at.table];
    PeerRows &held = table.peers[p_peer];
    const uint64_t token = held.token[p_at.row];
    const uint64_t revision = held.next_rev[p_at.row];
    if (token == 0 || revision == UINT64_MAX) {
        return 0;
    }
    held.next_rev[p_at.row] = revision + 1;
    if (revision > held.exposed_rev[p_at.row]) {
        held.exposed_rev[p_at.row] = revision;
    }
    LocalVector<Flight> *flights = sides[p_peer].flights.getptr(token);
    if (flights == nullptr) {
        flights = &sides[p_peer]
                       .flights.insert(token, LocalVector<Flight>())
                       ->value;
    }
    Flight made;
    made.slot = table.slots[p_at.row];
    made.gen = ref_of(made.slot).gen;
    made.table = p_at.table;
    made.ordinal = table.ordinals[p_at.row];
    made.family = uint8_t(table.family);
    made.revision = revision;
    made.pinned = p_pinned;
    if (!spare_images.is_empty()) {
        const uint32_t last = spare_images.size() - 1;
        made.image = std::move(spare_images[last]);
        spare_images.resize(last);
    }
    made.image.copy_from(p_image);
    append_moved(*flights, made);
    evict(*flights, spare_images);
    table.refresh_owed(p_at.row, p_peer);
    return revision;
}

const Flight *World::flight_of(
    uint32_t p_peer,
    uint64_t p_token,
    uint64_t p_revision
) const {
    const LocalVector<Flight> *flights
        = p_token != 0 ? sides[p_peer].flights.getptr(p_token) : nullptr;
    if (flights == nullptr) {
        return nullptr;
    }
    const int32_t found = flight_at(*flights, p_revision);
    return found >= 0 ? &(*flights)[uint32_t(found)] : nullptr;
}

const Flight *World::pinned_of(uint32_t p_peer, uint64_t p_token) const {
    const LocalVector<Flight> *flights
        = p_token != 0 ? sides[p_peer].flights.getptr(p_token) : nullptr;
    if (flights == nullptr) {
        return nullptr;
    }
    for (uint32_t at = flights->size(); at > 0; --at) {
        if ((*flights)[at - 1].pinned) {
            return &(*flights)[at - 1];
        }
    }
    return nullptr;
}

void World::arm_refresh(
    const RowAddress &p_at,
    uint32_t p_peer,
    int64_t p_tick
) {
    Table &table = *tables[p_at.table];
    PeerRows &held = table.peers[p_peer];
    held.refresh_at[p_at.row] = p_tick;
    if (p_tick < 0 || held.queued.get(p_at.row)
        || (table.flags[p_at.row] & Table::DECLARED) == 0) {
        return;
    }
    held.queued.set(p_at.row, true);
    Refresh entry;
    entry.at = p_tick;
    entry.lane = lane_key(table, p_at.row);
    LocalVector<Refresh> &heap = sides[p_peer].refreshes;
    heap.push_back(entry);
    sift_up(heap, heap.size() - 1);
}

void World::come_due(uint32_t p_peer, int64_t p_tick) {
    PeerSide &side = sides[p_peer];
    LocalVector<Refresh> &heap = side.refreshes;
    while (!heap.is_empty() && heap[0].at <= p_tick) {
        const Refresh top = heap[0];
        heap[0] = heap[heap.size() - 1];
        heap.resize(heap.size() - 1);
        if (!heap.is_empty()) {
            sift_down(heap, 0);
        }
        const RowAddress at = lane_row(top.lane);
        if (!at.valid()) {
            continue;
        }
        PeerRows &held = tables[at.table]->peers[p_peer];
        held.queued.set(at.row, false);
        const int64_t wanted = held.refresh_at[at.row];
        if (wanted < 0) {
            continue;
        }
        if (wanted > p_tick) {
            arm_refresh(at, p_peer, wanted);
            continue;
        }
        if (!held.due.get(at.row)) {
            held.due.set(at.row, true);
            side.due.push_back(top.lane);
        }
    }
}

void World::settle_due(uint32_t p_peer) {
    PeerSide &side = sides[p_peer];
    uint32_t kept = 0;
    for (uint32_t at = 0; at < side.due.size(); ++at) {
        const RowAddress row = lane_row(side.due[at]);
        if (!row.valid()
            || !tables[row.table]->peers[p_peer].due.get(row.row)) {
            continue;
        }
        side.due[kept] = side.due[at];
        ++kept;
    }
    side.due.resize(kept);
}

void World::stream_withdrawn(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    const uint32_t peer = peer_of(p_peer);
    if (peer == Table::NO_SLOT) {
        return;
    }
    LocalVector<Flight> *flights = sides[peer].flights.getptr(p_token);
    const int32_t found
        = flights != nullptr ? flight_at(*flights, p_revision) : -1;
    if (found >= 0) {
        flights->remove_at(uint32_t(found));
    }
}

wire::ReceiptVerdict World::stream_receipt(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    const uint32_t peer = peer_of(p_peer);
    LocalVector<Flight> *flights = peer != Table::NO_SLOT
        ? sides[peer].flights.getptr(p_token)
        : nullptr;
    if (flights == nullptr || flights->is_empty()) {
        const uint64_t *address = peer != Table::NO_SLOT
            ? sides[peer].tokens.getptr(p_token)
            : nullptr;
        const RowAddress at
            = address != nullptr ? lane_row(*address) : RowAddress();
        if (!at.valid()
            || tables[at.table]->peers[peer].token[at.row] != p_token) {
            receipts.unknown += 1;
            return wire::ReceiptVerdict::IGNORED;
        }
        const PeerRows &held = tables[at.table]->peers[peer];
        if (p_revision < wire::SNAPSHOT_FIRST_REVISION
            || p_revision > held.exposed_rev[at.row]) {
            receipts.unexposed += 1;
            return wire::ReceiptVerdict::UNEXPOSED;
        }
        if (p_revision <= held.acked_rev[at.row]) {
            receipts.ignored += 1;
            return wire::ReceiptVerdict::IGNORED;
        }
        receipts.evicted += 1;
        return wire::ReceiptVerdict::EVICTED;
    }
    const Flight &named = (*flights)[0];
    wire::StreamLane lane;
    lane.route = route_of(named.slot);
    lane.ordinal = named.ordinal;
    lane.family = wire::StreamFamily(named.family);
    const RowAddress at = lane_row(lane);
    EntityRef ref;
    ref.slot = named.slot;
    ref.gen = named.gen;
    if (!holds(ref) || !at.valid() || at.table != named.table
        || tables[at.table]->slots[at.row] != named.slot
        || tables[at.table]->peers[peer].token[at.row] != p_token) {
        receipts.stale += 1;
        return wire::ReceiptVerdict::IGNORED;
    }
    Table &table = *tables[at.table];
    PeerRows &held = table.peers[peer];
    if (p_revision < wire::SNAPSHOT_FIRST_REVISION
        || p_revision > held.exposed_rev[at.row]) {
        receipts.unexposed += 1;
        return wire::ReceiptVerdict::UNEXPOSED;
    }
    if (p_revision <= held.acked_rev[at.row]) {
        receipts.ignored += 1;
        return wire::ReceiptVerdict::IGNORED;
    }
    const int32_t found = flight_at(*flights, p_revision);
    if (found < 0) {
        receipts.evicted += 1;
        return wire::ReceiptVerdict::EVICTED;
    }
    table.promote(at.row, peer, p_revision, (*flights)[uint32_t(found)].image);
    uint32_t at_flight = 0;
    while (at_flight < flights->size()) {
        if ((*flights)[at_flight].revision <= p_revision) {
            spare(spare_images, (*flights)[at_flight]);
            flights->remove_at(at_flight);
            continue;
        }
        ++at_flight;
    }
    receipts.promoted += 1;
    return wire::ReceiptVerdict::PROMOTED;
}

} // namespace netw::store
