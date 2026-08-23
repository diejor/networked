#include "netw/api/spawner_roster.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/local_vector.hpp"

using namespace godot;

namespace netw {

bool NetwSpawnerRoster::enrol(Object *spawner) {
    if (spawner == nullptr) {
        return false;
    }
    const ObjectID id = gd::instance_id(spawner);
    if (spawners.has(int64_t(id))) {
        return false;
    }
    spawners.insert(int64_t(id), id);
    return true;
}

Array NetwSpawnerRoster::live() {
    Array out;
    LocalVector<int64_t> gone;
    for (const KeyValue<int64_t, ObjectID> &row : spawners) {
        Object *found = gd::instance_from_id(row.value);
        if (found == nullptr) {
            gone.push_back(row.key);
            continue;
        }
        out.push_back(found);
    }
    for (uint32_t index = 0; index < gone.size(); ++index) {
        spawners.erase(gone[index]);
    }
    return out;
}

int NetwSpawnerRoster::size() const {
    return int(spawners.size());
}

void NetwSpawnerRoster::note_producer(int64_t route, Object *spawner) {
    if (spawner == nullptr) {
        return;
    }
    producers[route] = gd::instance_id(spawner);
}

Object *NetwSpawnerRoster::take_producer(int64_t route) {
    const HashMap<int64_t, ObjectID>::Iterator found = producers.find(route);
    if (!found) {
        return nullptr;
    }
    Object *spawner = gd::instance_from_id(found->value);
    producers.remove(found);
    return spawner;
}

int NetwSpawnerRoster::produced_count(Object *spawner) const {
    if (spawner == nullptr) {
        return 0;
    }
    const ObjectID wanted = gd::instance_id(spawner);
    int total = 0;
    for (const KeyValue<int64_t, ObjectID> &row : producers) {
        if (row.value == wanted) {
            total += 1;
        }
    }
    return total;
}

void NetwSpawnerRoster::clear() {
    spawners.clear();
    producers.clear();
}

void NetwSpawnerRoster::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("enrol", "spawner"),
        &NetwSpawnerRoster::enrol
    );
    ClassDB::bind_method(D_METHOD("live"), &NetwSpawnerRoster::live);
    ClassDB::bind_method(D_METHOD("size"), &NetwSpawnerRoster::size);
    ClassDB::bind_method(
        D_METHOD("note_producer", "route", "spawner"),
        &NetwSpawnerRoster::note_producer
    );
    ClassDB::bind_method(
        D_METHOD("take_producer", "route"),
        &NetwSpawnerRoster::take_producer
    );
    ClassDB::bind_method(
        D_METHOD("produced_count", "spawner"),
        &NetwSpawnerRoster::produced_count
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwSpawnerRoster::clear);
}

} // namespace netw
