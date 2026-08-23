#include "netw/api/liveness_core.hpp"

#include <vector>

#include "godot/class_db.hpp"
#include "netw/entity_ids.hpp"
#include "netw/colors.hpp"
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
    const RID entity = entity_ids::mint();
    records.insert(entity, Record());
    return entity;
}

bool NetwLivenessCore::adopt(const RID &entity) {
    if (records.has(entity)) {
        return true;
    }
    if (!entity_ids::retain(entity)) {
        return false;
    }
    records.insert(entity, Record());
    return true;
}

bool NetwLivenessCore::entity_is_valid(const RID &entity) const {
    return records.has(entity);
}

int NetwLivenessCore::reserve_route() {
    route_counter += 1;
    return route_counter;
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

PackedInt32Array NetwLivenessCore::live_routes() const {
    PackedInt32Array out;
    for (const godot::KeyValue<int32_t, RID> &row : by_route) {
        const HashMap<RID, Record>::ConstIterator record = records.find(row.value);
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

void NetwLivenessCore::abandon_live(int route) {
    pending.erase(route);
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
        entity_ids::release(row.key);
    }
    records.clear();
    by_route.clear();
    pending.clear();
    route_counter = 0;
    frame_counter = 0;
}

NetwLivenessCore::NetwLivenessCore() {}

NetwLivenessCore::~NetwLivenessCore() {
    clear();
}

void NetwLivenessCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("entity_create"),
        &NetwLivenessCore::entity_create
    );
    ClassDB::bind_method(D_METHOD("adopt", "entity"), &NetwLivenessCore::adopt);
    ClassDB::bind_method(
        D_METHOD("entity_is_valid", "entity"),
        &NetwLivenessCore::entity_is_valid
    );
    ClassDB::bind_method(
        D_METHOD("reserve_route"),
        &NetwLivenessCore::reserve_route
    );
    ClassDB::bind_method(
        D_METHOD("bind_route", "entity", "route"),
        &NetwLivenessCore::bind_route
    );
    ClassDB::bind_method(
        D_METHOD("route_of", "entity"),
        &NetwLivenessCore::route_of
    );
    ClassDB::bind_method(
        D_METHOD("rid_from_route", "route"),
        &NetwLivenessCore::rid_from_route
    );
    ClassDB::bind_method(
        D_METHOD("epoch_of", "entity"),
        &NetwLivenessCore::epoch_of
    );
    ClassDB::bind_method(
        D_METHOD("state_of", "entity"),
        &NetwLivenessCore::state_of
    );
    ClassDB::bind_method(
        D_METHOD("route_state", "route"),
        &NetwLivenessCore::route_state
    );
    ClassDB::bind_method(
        D_METHOD("set_state", "entity", "state"),
        &NetwLivenessCore::set_state
    );
    ClassDB::bind_method(
        D_METHOD("live_routes"),
        &NetwLivenessCore::live_routes
    );
    ClassDB::bind_method(
        D_METHOD("route_count"),
        &NetwLivenessCore::route_count
    );
    ClassDB::bind_method(
        D_METHOD("bind_routes_data", "routes"),
        &NetwLivenessCore::bind_routes_data
    );
    ClassDB::bind_method(
        D_METHOD("tombstone_routes_data", "routes"),
        &NetwLivenessCore::tombstone_routes_data
    );
    ClassDB::bind_method(
        D_METHOD(
            "when_live",
            "route",
            "callback",
            "deadline",
            "on_clock",
            "on_timeout"
        ),
        &NetwLivenessCore::when_live
    );
    ClassDB::bind_method(
        D_METHOD("flush_live", "route"),
        &NetwLivenessCore::flush_live
    );
    ClassDB::bind_method(
        D_METHOD("abandon_live", "route"),
        &NetwLivenessCore::abandon_live
    );
    ClassDB::bind_method(
        D_METHOD("pending_live_count"),
        &NetwLivenessCore::pending_live_count
    );
    ClassDB::bind_method(
        D_METHOD("poll", "clock_tick"),
        &NetwLivenessCore::poll
    );
    ClassDB::bind_method(D_METHOD("frame"), &NetwLivenessCore::frame);
    ClassDB::bind_method(D_METHOD("clear"), &NetwLivenessCore::clear);

    BIND_ENUM_CONSTANT(STATE_UNKNOWN);
    BIND_ENUM_CONSTANT(STATE_LIVE);
    BIND_ENUM_CONSTANT(STATE_LINGERING);
    BIND_ENUM_CONSTANT(STATE_DEAD);
}

} // namespace netw
