#include "netw/call_park.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

int64_t NetwCallPark::park(int64_t sender, int64_t route, int64_t deadline) {
    if (active_count(sender) >= BUDGET) {
        refused_calls += 1;
        return -1;
    }
    Row row;
    row.id = next_id;
    row.sender = sender;
    row.route = route;
    row.deadline = deadline;
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

void NetwCallPark::sweep(int64_t now) {
    LocalVector<Row> kept;
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].active && now < rows[index].deadline) {
            kept.push_back(rows[index]);
        }
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

void NetwCallPark::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("park", "sender", "route", "deadline"),
        &NetwCallPark::park
    );
    ClassDB::bind_method(D_METHOD("resolve", "id"), &NetwCallPark::resolve);
    ClassDB::bind_method(D_METHOD("sweep", "now"), &NetwCallPark::sweep);
    ClassDB::bind_method(
        D_METHOD("active_count", "sender"),
        &NetwCallPark::active_count
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwCallPark::size);
    ClassDB::bind_method(D_METHOD("refused"), &NetwCallPark::refused);
    ClassDB::bind_method(
        D_METHOD("route_of", "id"),
        &NetwCallPark::route_of
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwCallPark::clear);
}

} // namespace netw
