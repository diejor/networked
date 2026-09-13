#include "netw/spawn/book.hpp"

#include "godot/callable.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/spawn/planner.hpp"

using namespace godot;

namespace netw::spawn {

void Book::arm(const Record &record) {
    armed[record.get_route()] = record;
}

bool Book::take_armed(int64_t route, Record &out) {
    const HashMap<int64_t, Record>::Iterator found = armed.find(route);
    if (!found) {
        return false;
    }
    out = found->value;
    armed.remove(found);
    return true;
}

bool Book::has_armed(int64_t route) const {
    return armed.has(route);
}

bool Book::drop_armed(int64_t route) {
    return armed.erase(route);
}

bool Book::books_node(Node *p_node) const {
    if (p_node == nullptr) {
        return false;
    }
    for (const KeyValue<int64_t, Record> &row : armed) {
        if (row.value.node() == p_node) {
            return true;
        }
    }
    for (const KeyValue<int64_t, Record> &row : spawned) {
        if (row.value.node() == p_node) {
            return true;
        }
    }
    return false;
}

int Book::armed_count() const {
    return int(armed.size());
}

Record *Book::issue(const Record &record) {
    spawned[record.get_route()] = record;
    return spawned_of(record.get_route());
}

Record *Book::spawned_of(int64_t route) {
    const HashMap<int64_t, Record>::Iterator found = spawned.find(route);
    return found ? &found->value : nullptr;
}

const Record *Book::spawned_of(int64_t route) const {
    const HashMap<int64_t, Record>::ConstIterator found = spawned.find(route);
    return found ? &found->value : nullptr;
}

bool Book::has_spawned(int64_t route) const {
    return spawned.has(route);
}

bool Book::drop_spawned(int64_t route) {
    return spawned.erase(route);
}

PackedInt64Array Book::spawned_routes() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Record> &row : spawned) {
        out.push_back(row.key);
    }
    return out;
}

int Book::spawned_count() const {
    return int(spawned.size());
}

PackedInt64Array Book::ancestry_order() const {
    PackedInt64Array routes;
    Dictionary parents;
    for (const KeyValue<int64_t, Record> &row : spawned) {
        routes.push_back(row.key);
        parents[row.key] = row.value.get_parent_route();
    }
    return Planner::ancestry_order(routes, parents);
}

PackedInt64Array Book::despawn_order(int64_t route) const {
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
            HashMap<int64_t, Record>::ConstIterator held = spawned.find(walk);
            if (held == spawned.end()) {
                break;
            }
            walk = held->value.get_parent_route();
            guard += 1;
        }
    }
    return out;
}

bool Book::parent_admits(int64_t route, int64_t peer) const {
    HashMap<int64_t, Record>::ConstIterator held = spawned.find(route);
    if (held == spawned.end()) {
        return true;
    }
    const int64_t parent = held->value.get_parent_route();
    if (parent <= 0) {
        return true;
    }
    HashMap<int64_t, Record>::ConstIterator above = spawned.find(parent);
    if (above == spawned.end()) {
        return true;
    }
    return above->value.has_recipient(peer);
}

bool Book::spawn_is_duplicate(int64_t route, int64_t state) const {
    if (received.has(route)) {
        return true;
    }
    return state == NetwLivenessCore::STATE_LIVE
        || state == NetwLivenessCore::STATE_LINGERING;
}

String Book::recipe_base(
    const Record *record,
    const String &fallback_scene_path,
    const String &fallback_name
) {
    if (record != nullptr) {
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

void Book::enroll_recv(int64_t route, Node *node) {
    received[route] = gd::instance_id(node);
}

bool Book::is_recv(int64_t route) const {
    return received.has(route);
}

bool Book::drop_recv(int64_t route) {
    return received.erase(route);
}

int Book::recv_count() const {
    return int(received.size());
}

void Book::clear() {
    for (const KeyValue<int64_t, Record> &row : armed) {
        Node *node = row.value.node();
        if (node == nullptr) {
            continue;
        }
        NETW_WARN(
            sys::SPAWN,
            "spawn::Book: node '%s' (%s) was still armed at session end. A "
            "replicate ran but the node never entered the tree.",
            node->get_name(),
            row.value.get_scene_path()
        );
    }
    armed.clear();
    spawned.clear();
    received.clear();
}

} // namespace netw::spawn
