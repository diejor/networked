#include "netw/spawn/spawner_roster.hpp"

#include "godot/local_vector.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/resource_uid.hpp"

using namespace godot;

namespace netw::spawn {

bool SpawnerRoster::enrol(Object *spawner) {
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

int SpawnerRoster::scene_index_for(Object *p_spawner, Node *p_node) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (spawner == nullptr || p_node == nullptr) {
        return -1;
    }
    ResourceUID *uid = ResourceUID::get_singleton();
    if (uid == nullptr) {
        return -1;
    }
    const String target = uid->ensure_path(p_node->get_scene_file_path());
    if (target.is_empty()) {
        return -1;
    }
    const int count = int(spawner->get_spawnable_scene_count());
    for (int at = 0; at < count; at++) {
        if (uid->ensure_path(spawner->get_spawnable_scene(at)) == target) {
            return at;
        }
    }
    return -1;
}

LocalVector<Object *> SpawnerRoster::live() {
    LocalVector<Object *> out;
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

int SpawnerRoster::size() const {
    return int(spawners.size());
}

void SpawnerRoster::note_producer(int64_t route, Object *spawner) {
    if (spawner == nullptr) {
        return;
    }
    producers[route] = gd::instance_id(spawner);
}

Node *SpawnerRoster::take_producer(int64_t route) {
    const HashMap<int64_t, ObjectID>::Iterator found = producers.find(route);
    if (!found) {
        return nullptr;
    }
    Node *spawner = Object::cast_to<Node>(gd::instance_from_id(found->value));
    producers.remove(found);
    return spawner;
}

int SpawnerRoster::produced_count(Object *spawner) const {
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

void SpawnerRoster::clear() {
    spawners.clear();
    producers.clear();
}

} // namespace netw::spawn
