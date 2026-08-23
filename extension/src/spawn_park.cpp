#include "netw/spawn_park.hpp"

#include "godot/class_db.hpp"
#include "netw/api/liveness_core.hpp"

using namespace godot;

namespace netw {

bool NetwSpawnPark::park(
    int64_t route,
    const PackedByteArray &payload,
    Wait wait,
    int64_t deadline
) {
    if (rows.has(route)) {
        return false;
    }
    Row row;
    row.payload = payload;
    row.wait = wait;
    row.deadline = deadline;
    rows.insert(route, row);
    return true;
}

bool NetwSpawnPark::has(int64_t route) const {
    return rows.has(route);
}

PackedByteArray NetwSpawnPark::take(int64_t route) {
    const HashMap<int64_t, Row>::Iterator found = rows.find(route);
    if (!found) {
        return PackedByteArray();
    }
    const PackedByteArray payload = found->value.payload;
    rows.remove(found);
    return payload;
}

bool NetwSpawnPark::cancel(int64_t route) {
    return rows.erase(route);
}

PackedInt64Array NetwSpawnPark::waiting_on(Wait wait) const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Row> &row : rows) {
        if (row.value.wait == wait) {
            out.push_back(row.key);
        }
    }
    return out;
}

bool NetwSpawnPark::is_expired(int64_t route, int64_t now) const {
    const HashMap<int64_t, Row>::ConstIterator found = rows.find(route);
    if (!found || found->value.wait != WAIT_SCENE) {
        return false;
    }
    return now >= found->value.deadline;
}

int NetwSpawnPark::size() const {
    return int(rows.size());
}

void NetwSpawnPark::clear() {
    rows.clear();
}

bool NetwSpawnPark::anchor_parks(int64_t state) {
    return state == NetwLivenessCore::STATE_UNKNOWN
        || state == NetwLivenessCore::STATE_DEAD;
}

void NetwSpawnPark::_bind_methods() {
    BIND_ENUM_CONSTANT(WAIT_ROUTE);
    BIND_ENUM_CONSTANT(WAIT_SCENE);

    ClassDB::bind_method(
        D_METHOD("park", "route", "payload", "wait", "deadline"),
        &NetwSpawnPark::park
    );
    ClassDB::bind_method(D_METHOD("has", "route"), &NetwSpawnPark::has);
    ClassDB::bind_method(D_METHOD("take", "route"), &NetwSpawnPark::take);
    ClassDB::bind_method(D_METHOD("cancel", "route"), &NetwSpawnPark::cancel);
    ClassDB::bind_method(
        D_METHOD("waiting_on", "wait"),
        &NetwSpawnPark::waiting_on
    );
    ClassDB::bind_method(
        D_METHOD("is_expired", "route", "now"),
        &NetwSpawnPark::is_expired
    );
    ClassDB::bind_static_method(
        "NetwSpawnPark",
        D_METHOD("anchor_parks", "state"),
        &NetwSpawnPark::anchor_parks
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwSpawnPark::size);
    ClassDB::bind_method(D_METHOD("clear"), &NetwSpawnPark::clear);
}

} // namespace netw
