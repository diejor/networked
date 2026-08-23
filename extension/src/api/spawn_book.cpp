#include "netw/api/spawn_book.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/spawn_planner.hpp"

using namespace godot;

namespace netw {

void NetwSpawnBook::arm(const Ref<NetwSpawnRecord> &record) {
    NETW_ERR_COND(
        record.is_null(),
        sys::SPAWN,
        "NetwSpawnBook.arm: there is no record to arm."
    );
    armed[record->get_route()] = record;
}

Ref<NetwSpawnRecord> NetwSpawnBook::take_armed(int64_t route) {
    const HashMap<int64_t, Ref<NetwSpawnRecord>>::Iterator found
        = armed.find(route);
    if (!found) {
        return Ref<NetwSpawnRecord>();
    }
    const Ref<NetwSpawnRecord> record = found->value;
    armed.remove(found);
    return record;
}

bool NetwSpawnBook::has_armed(int64_t route) const {
    return armed.has(route);
}

bool NetwSpawnBook::drop_armed(int64_t route) {
    return armed.erase(route);
}

Array NetwSpawnBook::armed_records() const {
    Array out;
    for (const KeyValue<int64_t, Ref<NetwSpawnRecord>> &row : armed) {
        out.push_back(row.value);
    }
    return out;
}

int NetwSpawnBook::armed_count() const {
    return int(armed.size());
}

void NetwSpawnBook::issue(const Ref<NetwSpawnRecord> &record) {
    NETW_ERR_COND(
        record.is_null(),
        sys::SPAWN,
        "NetwSpawnBook.issue: there is no record to issue."
    );
    spawned[record->get_route()] = record;
}

Ref<NetwSpawnRecord> NetwSpawnBook::spawned_of(int64_t route) const {
    const HashMap<int64_t, Ref<NetwSpawnRecord>>::ConstIterator found
        = spawned.find(route);
    return found ? found->value : Ref<NetwSpawnRecord>();
}

bool NetwSpawnBook::has_spawned(int64_t route) const {
    return spawned.has(route);
}

bool NetwSpawnBook::drop_spawned(int64_t route) {
    return spawned.erase(route);
}

PackedInt64Array NetwSpawnBook::spawned_routes() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Ref<NetwSpawnRecord>> &row : spawned) {
        out.push_back(row.key);
    }
    return out;
}

Array NetwSpawnBook::spawned_records() const {
    Array out;
    for (const KeyValue<int64_t, Ref<NetwSpawnRecord>> &row : spawned) {
        out.push_back(row.value);
    }
    return out;
}

int NetwSpawnBook::spawned_count() const {
    return int(spawned.size());
}

PackedInt64Array NetwSpawnBook::ancestry_order() const {
    PackedInt64Array routes;
    Dictionary parents;
    for (const KeyValue<int64_t, Ref<NetwSpawnRecord>> &row : spawned) {
        routes.push_back(row.key);
        parents[row.key] = row.value->get_parent_route();
    }
    return NetwSpawnPlanner::ancestry_order(routes, parents);
}

PackedInt64Array NetwSpawnBook::despawn_order(int64_t route) const {
    PackedInt64Array out;
    if (!spawned.has(route)) {
        return out;
    }
    const PackedInt64Array ordered = ancestry_order();
    for (int at = ordered.size() - 1; at >= 0; --at) {
        const int64_t candidate = ordered[at];
        int64_t walk = candidate;
        int guard = 0;
        while (walk != 0 && guard <= ordered.size()) {
            if (walk == route) {
                out.push_back(candidate);
                break;
            }
            HashMap<int64_t, Ref<NetwSpawnRecord>>::ConstIterator held
                = spawned.find(walk);
            if (held == spawned.end()) {
                break;
            }
            walk = held->value->get_parent_route();
            guard += 1;
        }
    }
    return out;
}

bool NetwSpawnBook::parent_admits(int64_t route, int64_t peer) const {
    HashMap<int64_t, Ref<NetwSpawnRecord>>::ConstIterator held
        = spawned.find(route);
    if (held == spawned.end()) {
        return true;
    }
    const int64_t parent = held->value->get_parent_route();
    if (parent <= 0) {
        return true;
    }
    HashMap<int64_t, Ref<NetwSpawnRecord>>::ConstIterator above
        = spawned.find(parent);
    if (above == spawned.end()) {
        return true;
    }
    return above->value->has_recipient(peer);
}

bool NetwSpawnBook::spawn_is_duplicate(int64_t route, int64_t state) const {
    if (received.has(route)) {
        return true;
    }
    return state == NetwLivenessCore::STATE_LIVE
        || state == NetwLivenessCore::STATE_LINGERING;
}

String NetwSpawnBook::recipe_base(
    const Ref<NetwSpawnRecord> &record,
    const String &fallback_scene_path,
    const String &fallback_name
) {
    if (record.is_valid()) {
        switch (Recipe(record->get_recipe())) {
            case RECIPE_SCENE:
                return record->get_scene_path().get_file().get_basename();
            case RECIPE_FN:
                return String(record->get_fn_method());
            case RECIPE_FN_REGISTRY:
                return String(record->get_fn_registry_id());
            default:
                break;
        }
    }
    if (!fallback_scene_path.is_empty()) {
        return fallback_scene_path.get_file().get_basename();
    }
    return fallback_name;
}

void NetwSpawnBook::enroll_recv(int64_t route, Node *node) {
    received[route] = gd::instance_id(node);
}

bool NetwSpawnBook::is_recv(int64_t route) const {
    return received.has(route);
}

bool NetwSpawnBook::drop_recv(int64_t route) {
    return received.erase(route);
}

int NetwSpawnBook::recv_count() const {
    return int(received.size());
}

void NetwSpawnBook::clear() {
    for (const KeyValue<int64_t, Ref<NetwSpawnRecord>> &row : armed) {
        Node *node = row.value->node();
        if (node == nullptr) {
            continue;
        }
        NETW_WARN(
            sys::SPAWN,
            "NetwSpawnBook: node '%s' (%s) was still armed at session end. A "
            "replicate ran but the node never entered the tree.",
            node->get_name(),
            row.value->get_scene_path()
        );
    }
    armed.clear();
    spawned.clear();
    received.clear();
}

void NetwSpawnBook::_bind_methods() {
    BIND_ENUM_CONSTANT(RECIPE_SCENE);
    BIND_ENUM_CONSTANT(RECIPE_FN);
    BIND_ENUM_CONSTANT(RECIPE_SPAWNER);
    BIND_ENUM_CONSTANT(RECIPE_ADOPT);
    BIND_ENUM_CONSTANT(RECIPE_FN_REGISTRY);

    ClassDB::bind_method(D_METHOD("arm", "record"), &NetwSpawnBook::arm);
    ClassDB::bind_method(
        D_METHOD("take_armed", "route"),
        &NetwSpawnBook::take_armed
    );
    ClassDB::bind_method(
        D_METHOD("has_armed", "route"),
        &NetwSpawnBook::has_armed
    );
    ClassDB::bind_method(
        D_METHOD("drop_armed", "route"),
        &NetwSpawnBook::drop_armed
    );
    ClassDB::bind_method(
        D_METHOD("armed_records"),
        &NetwSpawnBook::armed_records
    );
    ClassDB::bind_method(D_METHOD("armed_count"), &NetwSpawnBook::armed_count);

    ClassDB::bind_method(D_METHOD("issue", "record"), &NetwSpawnBook::issue);
    ClassDB::bind_method(
        D_METHOD("spawned_of", "route"),
        &NetwSpawnBook::spawned_of
    );
    ClassDB::bind_method(
        D_METHOD("has_spawned", "route"),
        &NetwSpawnBook::has_spawned
    );
    ClassDB::bind_method(
        D_METHOD("drop_spawned", "route"),
        &NetwSpawnBook::drop_spawned
    );
    ClassDB::bind_method(
        D_METHOD("spawned_routes"),
        &NetwSpawnBook::spawned_routes
    );
    ClassDB::bind_method(
        D_METHOD("spawned_records"),
        &NetwSpawnBook::spawned_records
    );
    ClassDB::bind_method(
        D_METHOD("spawned_count"),
        &NetwSpawnBook::spawned_count
    );
    ClassDB::bind_method(
        D_METHOD("ancestry_order"),
        &NetwSpawnBook::ancestry_order
    );
    ClassDB::bind_method(
        D_METHOD("despawn_order", "route"),
        &NetwSpawnBook::despawn_order
    );
    ClassDB::bind_method(
        D_METHOD("parent_admits", "route", "peer"),
        &NetwSpawnBook::parent_admits
    );
    ClassDB::bind_method(
        D_METHOD("spawn_is_duplicate", "route", "state"),
        &NetwSpawnBook::spawn_is_duplicate
    );
    ClassDB::bind_static_method(
        "NetwSpawnBook",
        D_METHOD("recipe_base", "record", "fallback_scene_path",
                 "fallback_name"),
        &NetwSpawnBook::recipe_base
    );

    ClassDB::bind_method(
        D_METHOD("enroll_recv", "route", "node"),
        &NetwSpawnBook::enroll_recv
    );
    ClassDB::bind_method(D_METHOD("is_recv", "route"), &NetwSpawnBook::is_recv);
    ClassDB::bind_method(
        D_METHOD("drop_recv", "route"),
        &NetwSpawnBook::drop_recv
    );
    ClassDB::bind_method(D_METHOD("recv_count"), &NetwSpawnBook::recv_count);

    ClassDB::bind_method(D_METHOD("clear"), &NetwSpawnBook::clear);
}

} // namespace netw
