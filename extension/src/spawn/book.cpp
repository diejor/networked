#include "netw/spawn/book.hpp"

#include "godot/callable.hpp"
#include "netw/held_bytes.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/spawn/planner.hpp"

using namespace godot;

namespace netw::spawn {

void Book::hold_node(const Record &record) {
    node_holds[uint64_t(record.get_node_id())] += 1;
}

void Book::release_node(const Record &record) {
    const uint64_t key = uint64_t(record.get_node_id());
    int *holds = node_holds.getptr(key);
    if (holds != nullptr && --(*holds) <= 0) {
        node_holds.erase(key);
    }
}

void Book::arm(const Record &record) {
    const Record *held = armed.getptr(record.get_route());
    if (held != nullptr) {
        release_node(*held);
    }
    armed[record.get_route()] = record;
    hold_node(record);
}

bool Book::take_armed(int64_t route, Record &out) {
    const HashMap<int64_t, Record>::Iterator found = armed.find(route);
    if (!found) {
        return false;
    }
    out = found->value;
    release_node(found->value);
    armed.remove(found);
    return true;
}

bool Book::has_armed(int64_t route) const {
    return armed.has(route);
}

bool Book::drop_armed(int64_t route) {
    const HashMap<int64_t, Record>::Iterator found = armed.find(route);
    if (!found) {
        return false;
    }
    release_node(found->value);
    armed.remove(found);
    return true;
}

bool Book::books_node(Node *p_node) const {
    if (p_node == nullptr) {
        return false;
    }
    return node_holds.has(uint64_t(gd::instance_id(p_node)));
}

int Book::armed_count() const {
    return int(armed.size());
}

Record *Book::issue(const Record &record) {
    const Record *held = spawned.getptr(record.get_route());
    if (held != nullptr) {
        release_node(*held);
    }
    spawned[record.get_route()] = record;
    hold_node(record);
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
    const HashMap<int64_t, Record>::Iterator found = spawned.find(route);
    if (!found) {
        return false;
    }
    release_node(found->value);
    spawned.remove(found);
    return true;
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

PackedInt64Array Book::detach_roots(
    int64_t route,
    const PackedInt64Array &detaching
) const {
    PackedInt64Array out;
    for (int at = 0; at < detaching.size(); ++at) {
        const int64_t candidate = detaching[at];
        if (candidate == route) {
            continue;
        }
        HashMap<int64_t, Record>::ConstIterator held = spawned.find(candidate);
        bool shadowed = false;
        int guard = 0;
        while (held != spawned.end() && guard <= int(spawned.size())) {
            const int64_t above = held->value.get_parent_route();
            if (above == route || above <= 0) {
                break;
            }
            if (detaching.has(above)) {
                shadowed = true;
                break;
            }
            held = spawned.find(above);
            guard += 1;
        }
        if (!shadowed) {
            out.push_back(candidate);
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
    return above != spawned.end() && above->value.has_recipient(peer);
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

Book::Received Book::received_of(const Record &record) {
    Received out;
    out.node = record.get_node_id();
    out.recipe = int32_t(record.get_recipe());
    out.scene_index = int32_t(record.get_scene_index());
    switch (Recipe(record.get_recipe())) {
        case RECIPE_FN:
            out.source = gd::instance_id(record.fn_host());
            out.fn = record.get_fn_method();
            out.payload = record.get_fn_args();
            break;
        case RECIPE_FN_REGISTRY:
            out.fn = record.get_fn_registry_id();
            out.payload = record.get_fn_args();
            break;
        case RECIPE_SPAWNER:
            out.source = gd::instance_id(record.spawner());
            out.payload = record.get_custom_data();
            break;
        case RECIPE_ADOPT:
            out.source = gd::instance_id(record.origin());
            break;
        case RECIPE_SCENE:
            break;
    }
    return out;
}

void Book::enroll_recv(int64_t route, const Received &recipe) {
    received[route] = recipe;
}

bool Book::is_recv(int64_t route) const {
    return received.has(route);
}

bool Book::drop_recv(int64_t route) {
    return received.erase(route);
}

bool Book::take_recv(int64_t route, Record &out) {
    const HashMap<int64_t, Received>::Iterator found = received.find(route);
    if (!found) {
        return false;
    }
    const Received recipe = found->value;
    received.remove(found);
    Node *node = Object::cast_to<Node>(gd::instance_from_id(recipe.node));
    Node *source = Object::cast_to<Node>(gd::instance_from_id(recipe.source));
    out.set_route(route);
    out.set_recipe(recipe.recipe);
    out.set_scene_index(recipe.scene_index);
    out.bind_node(node);
    switch (Recipe(recipe.recipe)) {
        case RECIPE_SCENE:
            if (node != nullptr) {
                out.set_scene_path(node->get_scene_file_path());
            }
            break;
        case RECIPE_FN:
            out.bind_fn_host(source);
            out.set_fn_method(recipe.fn);
            out.set_fn_args(recipe.payload);
            break;
        case RECIPE_FN_REGISTRY:
            out.set_fn_registry_id(recipe.fn);
            out.set_fn_args(recipe.payload);
            break;
        case RECIPE_SPAWNER:
            out.bind_spawner(Object::cast_to<MultiplayerSpawner>(source));
            out.set_custom_data(recipe.payload);
            break;
        case RECIPE_ADOPT:
            out.bind_origin(source);
            break;
    }
    return true;
}

PackedInt64Array Book::recv_routes() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Received> &row : received) {
        out.push_back(row.key);
    }
    return out;
}

PackedInt64Array Book::authored_routes() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Received> &row : received) {
        if (row.value.authored) {
            out.push_back(row.key);
        }
    }
    return out;
}

int Book::recv_count() const {
    return int(received.size());
}

int64_t Book::held_bytes() const {
    int64_t bytes = held::map(armed) + held::map(spawned) + held::map(received)
        + held::map(node_holds);
    for (const KeyValue<int64_t, Received> &row : received) {
        if (row.value.payload.get_type() == Variant::ARRAY) {
            const Array args = row.value.payload;
            bytes += int64_t(args.size()) * int64_t(sizeof(Variant));
        }
    }
    return bytes;
}

namespace {

bool ends_with_session(int32_t p_recipe, bool p_authored) {
    if (p_recipe == Book::RECIPE_ADOPT) {
        return false;
    }
    return p_recipe != Book::RECIPE_SPAWNER || !p_authored;
}

} // namespace

LocalVector<ObjectID> Book::session_nodes() const {
    LocalVector<ObjectID> out;
    for (const KeyValue<int64_t, Record> &row : armed) {
        if (ends_with_session(row.value.get_recipe(), true)) {
            out.push_back(row.value.get_node_id());
        }
    }
    for (const KeyValue<int64_t, Record> &row : spawned) {
        if (ends_with_session(row.value.get_recipe(), true)) {
            out.push_back(row.value.get_node_id());
        }
    }
    for (const KeyValue<int64_t, Received> &row : received) {
        if (ends_with_session(row.value.recipe, row.value.authored)) {
            out.push_back(row.value.node);
        }
    }
    return out;
}

bool Book::armed_scene_path(ObjectID p_node, String &r_path) const {
    for (const KeyValue<int64_t, Record> &row : armed) {
        if (row.value.get_node_id() == p_node) {
            r_path = row.value.get_scene_path();
            return true;
        }
    }
    return false;
}

void Book::clear() {
    armed.clear();
    spawned.clear();
    received.clear();
    node_holds.clear();
}

} // namespace netw::spawn
