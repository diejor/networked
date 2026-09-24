#include "netw/liveness_core.hpp"

#include <vector>

#include "netw/colors.hpp"
#include "netw/entity/ids.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

int rank_of(NetwLivenessCore::State state) {
    switch (state) {
        case NetwLivenessCore::STATE_UNKNOWN:
            return 0;
        case NetwLivenessCore::STATE_LIVE:
            return 1;
        case NetwLivenessCore::STATE_LINGERING:
            return 2;
        case NetwLivenessCore::STATE_DEAD:
            return 3;
        case NetwLivenessCore::STATE_ABSENT:
            return 1;
    }
    return 0;
}

} // namespace

NetwLivenessCore::Record *NetwLivenessCore::record_of(const RID &entity) const {
    HashMap<RID, Record>::Iterator found
        = const_cast<HashMap<RID, Record> &>(records).find(entity);
    return found ? &found->value : nullptr;
}

RID NetwLivenessCore::entity_create() {
    const RID entity = netw::entity::mint();
    records.insert(entity, Record());
    return entity;
}

bool NetwLivenessCore::adopt(const RID &entity) {
    if (records.has(entity)) {
        return true;
    }
    if (!netw::entity::retain(entity)) {
        return false;
    }
    records.insert(entity, Record());
    return true;
}

bool NetwLivenessCore::entity_is_valid(const RID &entity) const {
    return records.has(entity);
}

int NetwLivenessCore::reserve_route() {
    if (!leased) {
        route_counter += 1;
        return route_counter;
    }
    while (!lease.is_empty()) {
        LeaseBlock &block = lease[0];
        if (block.next < block.end) {
            const int32_t route = block.next;
            block.next += 1;
            minted_high = MAX(minted_high, route);
            return route;
        }
        lease.remove_at(0);
    }
    return 0;
}

int NetwLivenessCore::reserve_block(int count) {
    if (count <= 0) {
        return 0;
    }
    const int32_t base = route_counter + 1;
    route_counter += count;
    return base;
}

void NetwLivenessCore::install_lease(int base, int count) {
    leased = true;
    if (base <= 0 || count <= 0) {
        return;
    }
    LeaseBlock block;
    block.next = base;
    block.end = base + count;
    lease.push_back(block);
}

bool NetwLivenessCore::is_leased() const {
    return leased;
}

int NetwLivenessCore::route_floor() const {
    int floor = MAX(route_counter, minted_high);
    for (const KeyValue<int32_t, RID> &held : by_route) {
        floor = MAX(floor, int(held.key));
    }
    for (const LeaseBlock &block : lease) {
        floor = MAX(floor, int(block.end) - 1);
    }
    for (const LeaseBlock &block : grants) {
        floor = MAX(floor, int(block.end) - 1);
    }
    return floor;
}

int NetwLivenessCore::lease_remaining() const {
    int remaining = 0;
    for (const LeaseBlock &block : lease) {
        remaining += block.end - block.next;
    }
    return remaining;
}

int NetwLivenessCore::grant_lease(int peer, int count) {
    const int base = reserve_block(count);
    if (base == 0) {
        return 0;
    }
    LeaseBlock block;
    block.peer = peer;
    block.next = base;
    block.end = base + count;
    grants.push_back(block);
    return base;
}

int64_t NetwLivenessCore::lease_block_holding(int peer, int route) const {
    for (uint32_t at = 0; at < grants.size(); at++) {
        const LeaseBlock &block = grants[at];
        if (block.peer == peer && route >= block.next && route < block.end) {
            return int64_t(at);
        }
    }
    return -1;
}

bool NetwLivenessCore::lease_holds(int peer, int route) const {
    return lease_block_holding(peer, route) >= 0;
}

bool NetwLivenessCore::spend_lease(int peer, int route) {
    const int64_t found = lease_block_holding(peer, route);
    if (found < 0) {
        return false;
    }
    grants[uint32_t(found)].next = route + 1;
    for (int64_t at = int64_t(grants.size()) - 1; at >= 0; at--) {
        const LeaseBlock &block = grants[uint32_t(at)];
        if (block.peer != peer) {
            continue;
        }
        if (at < found || block.next >= block.end) {
            grants.remove_at(uint32_t(at));
        }
    }
    return true;
}

int NetwLivenessCore::granted_remaining(int peer) const {
    int remaining = 0;
    for (const LeaseBlock &block : grants) {
        if (block.peer == peer) {
            remaining += block.end - block.next;
        }
    }
    return remaining;
}

void NetwLivenessCore::abandon_lease(int peer) {
    for (int64_t at = int64_t(grants.size()) - 1; at >= 0; at--) {
        if (grants[uint32_t(at)].peer == peer) {
            grants.remove_at(uint32_t(at));
        }
    }
}

bool NetwLivenessCore::bind_route(const RID &entity, int route) {
    if (route <= 0) {
        return false;
    }
    Record *record = record_of(entity);
    if (record == nullptr) {
        return false;
    }

    const RID held = rid_from_route(route);
    if (held.is_valid() && held != entity) {
        return false;
    }
    if (record->state == STATE_DEAD) {
        record->epoch += 1;
        record->anchor = 1;
        record->anchor_author = 0;
    }

    record->route = route;
    record->state = STATE_LIVE;
    by_route[route] = entity;
    return true;
}

int NetwLivenessCore::epoch_of(const RID &entity) const {
    const Record *record = record_of(entity);
    return record ? record->epoch : -1;
}

int NetwLivenessCore::route_epoch(int route) const {
    const Record *record = record_of(rid_from_route(route));
    return record != nullptr ? record->epoch : 0;
}

int NetwLivenessCore::route_wire_epoch(int route) const {
    const Record *record = record_of(rid_from_route(route));
    return record != nullptr ? record->wire_epoch : -1;
}

bool NetwLivenessCore::epoch_admits(int route, int epoch) const {
    const Record *record = record_of(rid_from_route(route));
    return record == nullptr || epoch >= record->wire_epoch;
}

bool NetwLivenessCore::adopt_epoch(int route, int epoch) {
    Record *record = record_of(rid_from_route(route));
    if (record == nullptr || epoch < record->wire_epoch) {
        return false;
    }
    record->wire_epoch = epoch;
    return true;
}

uint64_t NetwLivenessCore::route_anchor(int route) const {
    const Record *record = record_of(rid_from_route(route));
    return record != nullptr ? record->anchor : 0;
}

uint64_t NetwLivenessCore::route_anchor_author(int route) const {
    const Record *record = record_of(rid_from_route(route));
    return record != nullptr ? record->anchor_author : 0;
}

bool NetwLivenessCore::set_route_anchor(
    int route,
    uint64_t anchor,
    uint64_t author
) {
    Record *record = record_of(rid_from_route(route));
    if (record == nullptr || anchor == 0) {
        return false;
    }
    record->anchor = anchor;
    record->anchor_author = author;
    return true;
}

int NetwLivenessCore::route_of(const RID &entity) const {
    const Record *record = record_of(entity);
    return record ? record->route : 0;
}

RID NetwLivenessCore::rid_from_route(int route) const {
    const godot::HashMap<int32_t, RID>::ConstIterator found
        = by_route.find(route);
    return found != by_route.end() ? found->value : RID();
}

NetwLivenessCore::State NetwLivenessCore::state_of(const RID &entity) const {
    const Record *record = record_of(entity);
    return record ? record->state : STATE_UNKNOWN;
}

NetwLivenessCore::State NetwLivenessCore::route_state(int route) const {
    return state_of(rid_from_route(route));
}

bool NetwLivenessCore::set_state(const RID &entity, State state) {
    if (state == STATE_ABSENT) {
        return false;
    }
    Record *record = record_of(entity);
    if (record == nullptr) {
        return false;
    }
    if (rank_of(state) <= rank_of(record->state)) {
        return false;
    }
    record->state = state;
    return true;
}

bool NetwLivenessCore::hide_route(int route) {
    Record *record = record_of(rid_from_route(route));
    if (record == nullptr || record->state != STATE_LIVE) {
        return false;
    }
    record->state = STATE_ABSENT;
    return true;
}

PackedInt32Array NetwLivenessCore::live_routes() const {
    PackedInt32Array out;
    for (const godot::KeyValue<int32_t, RID> &row : by_route) {
        const HashMap<RID, Record>::ConstIterator record
            = records.find(row.value);
        if (record && record->value.state == STATE_LIVE) {
            out.push_back(row.key);
        }
    }
    out.sort();
    return out;
}

int NetwLivenessCore::route_count() const {
    return static_cast<int>(by_route.size());
}

void NetwLivenessCore::bind_routes_data(const PackedInt64Array &routes) {
    NETW_ZONE_NC("NetwLivenessCore bind routes", colors::LIVENESS);
    NETW_ZONE_VALUE(routes.size());
    for (int index = 0; index < routes.size(); ++index) {
        const int route = int(routes[index]);
        if (route <= 0) {
            continue;
        }
        const RID held = rid_from_route(route);
        const RID entity = held.is_valid() ? held : entity_create();
        if (!bind_route(entity, route)) {
            continue;
        }
        flush_live(route);
    }
}

void NetwLivenessCore::tombstone_routes_data(const PackedInt64Array &routes) {
    NETW_ZONE_NC("NetwLivenessCore tombstone routes", colors::LIVENESS);
    NETW_ZONE_VALUE(routes.size());
    for (int index = 0; index < routes.size(); ++index) {
        const int route = int(routes[index]);
        if (route <= 0) {
            continue;
        }
        const RID entity = rid_from_route(route);
        if (entity.is_valid()) {
            set_state(entity, STATE_DEAD);
        }
    }
}

void NetwLivenessCore::when_live(
    int route,
    const Callable &callback,
    int deadline,
    bool on_clock,
    const Callable &on_timeout
) {
    if (route_state(route) == STATE_LIVE) {
        callback.call();
        return;
    }
    Pending entry;
    entry.callback = callback;
    entry.on_timeout = on_timeout;
    entry.deadline = int32_t(deadline);
    entry.on_clock = on_clock;
    pending[route].push_back(entry);
}

void NetwLivenessCore::flush_live(int route) {
    const godot::HashMap<int32_t, LocalVector<Pending>>::Iterator found
        = pending.find(route);
    if (found == pending.end()) {
        return;
    }
    const LocalVector<Pending> waiting(found->value);
    pending.remove(found);
    for (const Pending &entry : waiting) {
        if (entry.callback.is_valid()) {
            entry.callback.call();
        }
    }
}

void NetwLivenessCore::fail_live(int route) {
    const godot::HashMap<int32_t, LocalVector<Pending>>::Iterator found
        = pending.find(route);
    if (found == pending.end()) {
        return;
    }
    const LocalVector<Pending> waiting(found->value);
    pending.remove(found);
    for (const Pending &entry : waiting) {
        if (entry.on_timeout.is_valid()) {
            entry.on_timeout.call();
        }
    }
}

int NetwLivenessCore::pending_live_count() const {
    int total = 0;
    for (const godot::KeyValue<int32_t, LocalVector<Pending>> &row : pending) {
        total += int(row.value.size());
    }
    return total;
}

PackedInt32Array NetwLivenessCore::poll(int clock_tick) {
    NETW_ZONE_NC("NetwLivenessCore poll", colors::LIVENESS);
    NETW_ZONE_VALUE(pending_live_count());
    frame_counter += 1;
    return sweep_pending(clock_tick);
}

int NetwLivenessCore::frame() const {
    return frame_counter;
}

PackedInt32Array NetwLivenessCore::sweep_pending(int clock_tick) {
    LocalVector<Callable> fired;
    LocalVector<int32_t> emptied;

    for (godot::KeyValue<int32_t, LocalVector<Pending>> &row : pending) {
        LocalVector<Pending> remaining;
        for (const Pending &entry : row.value) {
            const int current = entry.on_clock ? clock_tick : frame_counter;
            if (current >= entry.deadline) {
                if (entry.on_timeout.is_valid()) {
                    fired.push_back(entry.on_timeout);
                }
                continue;
            }
            remaining.push_back(entry);
        }
        if (remaining.is_empty()) {
            emptied.push_back(row.key);
        } else {
            row.value = remaining;
        }
    }

    PackedInt32Array expired;
    for (const int32_t route : emptied) {
        pending.erase(route);
        expired.push_back(route);
    }
    expired.sort();

    for (const Callable &callback : fired) {
        callback.call();
    }
    return expired;
}

void NetwLivenessCore::clear() {
    for (const KeyValue<RID, Record> &row : records) {
        netw::entity::release(row.key);
    }
    records.clear();
    by_route.clear();
    pending.clear();
    lease.clear();
    grants.clear();
    leased = false;
    route_counter = 0;
    minted_high = 0;
    frame_counter = 0;
}

NetwLivenessCore::NetwLivenessCore() {
}

NetwLivenessCore::~NetwLivenessCore() {
    clear();
}

} // namespace netw
