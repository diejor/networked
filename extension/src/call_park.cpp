#include "netw/call_park.hpp"

#include "netw/liveness_core.hpp"

using namespace godot;

namespace netw {

int64_t NetwCallPark::park(
    int64_t sender,
    int64_t route,
    int64_t now,
    int64_t timeout
) {
    if (active_count(sender) >= BUDGET) {
        refused_calls += 1;
        return -1;
    }
    Row row;
    row.id = next_id;
    row.sender = sender;
    row.route = route;
    row.deadline = now + timeout;
    row.span = timeout;
    rows.push_back(row);
    next_id += 1;
    return row.id;
}

bool NetwCallPark::resolve(int64_t id) {
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].id != id) {
            continue;
        }
        if (!rows[index].active) {
            return false;
        }
        rows[index].active = false;
        return true;
    }
    return false;
}

void NetwCallPark::sweep(int64_t now, const NetwLivenessCore &liveness) {
    LocalVector<Row> kept;
    for (uint32_t index = 0; index < rows.size(); ++index) {
        Row row = rows[index];
        if (!row.active) {
            continue;
        }
        if (liveness.is_held(int(row.route))) {
            row.deadline = now + row.span;
        } else if (now >= row.deadline) {
            continue;
        }
        kept.push_back(row);
    }
    rows = kept;
}

int NetwCallPark::active_count(int64_t sender) const {
    int total = 0;
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].sender == sender) {
            total += 1;
        }
    }
    return total;
}

int NetwCallPark::size() const {
    return int(rows.size());
}

int64_t NetwCallPark::route_of(int64_t id) const {
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].id == id) {
            return rows[index].route;
        }
    }
    return 0;
}

void NetwCallPark::clear() {
    rows.clear();
    refused_calls = 0;
}

} // namespace netw
