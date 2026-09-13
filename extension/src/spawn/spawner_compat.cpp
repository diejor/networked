#include "netw/spawn/spawner_compat.hpp"

#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"

using namespace godot;

namespace netw::spawn {

namespace {

const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_ENDED = "session_ended";
const char *SIG_NODE_ADDED = "node_added";
const char *SIG_READY = "ready";
const char *SIG_SPAWNED = "spawned";
const char *SIG_DESPAWNED = "despawned";
const char *KEY_CLEAR_SESSION = "spawner-compat-clear-session";

Variant duplicate_arg(const Variant &p_data) {
    if (p_data.get_type() == Variant::ARRAY) {
        return Array(p_data).duplicate(true);
    }
    if (p_data.get_type() == Variant::DICTIONARY) {
        return Dictionary(p_data).duplicate(true);
    }
    return p_data;
}

} // namespace

NetwMultiplayer *SpawnerCompat::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

Node *SpawnerCompat::session_root() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->session_root() : nullptr;
}

void SpawnerCompat::set_core(Object *p_core) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_core);
    core_id = gd::instance_id(plane);
    if (plane == nullptr) {
        return;
    }
    const Callable entered
        = callable_mp(plane, &NetwMultiplayer::spawner_session_entered);
    if (!plane->is_connected(SIG_SESSION_ENTERED, entered)) {
        plane->connect(SIG_SESSION_ENTERED, entered);
    }
    const Callable ended
        = callable_mp(plane, &NetwMultiplayer::spawner_session_ended);
    if (!plane->is_connected(SIG_SESSION_ENDED, ended)) {
        plane->connect(SIG_SESSION_ENDED, ended);
    }
}

void SpawnerCompat::set_spawn_seams(
    const Callable &p_booked_reader,
    const Callable &p_arm_consumed
) {
    booked_reader = p_booked_reader;
    arm_consumed = p_arm_consumed;
}

Error SpawnerCompat::consume(Node *p_node, Object *p_spawner) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (p_node == nullptr || spawner == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    if (!booked_reader.is_valid() || !arm_consumed.is_valid()) {
        return ERR_UNCONFIGURED;
    }
    register_spawner(spawner);
    if (bool(booked_reader.call(p_node))) {
        return OK;
    }

    const int64_t nid = int64_t(gd::instance_id(p_node));
    int scene_index = -1;
    Variant data;
    HashMap<int64_t, Variant>::Iterator captured = custom_args.find(nid);
    if (captured) {
        data = captured->value;
        custom_args.remove(captured);
    } else {
        scene_index = SpawnerRoster::scene_index_for(spawner, p_node);
        if (scene_index < 0) {
            drops_uncaptured_custom += 1;
            NETW_WARN(
                sys::SPAWN,
                "'%s' has no matching spawnable scene and no captured spawn "
                "argument, dropping the consumed spawn",
                p_node->get_name()
            );
            return OK;
        }
    }

    arm_consumed.call(p_node, spawner, scene_index, data);
    authoring::apply_spawner(p_node, spawner);
    return OK;
}

Error SpawnerCompat::consume_remove(Node *, Object *) {
    return OK;
}

void SpawnerCompat::register_spawner(Object *p_spawner) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (spawner == nullptr) {
        return;
    }
    roster.enrol(spawner);
    wrap_spawner(spawner);
}

void SpawnerCompat::wrap_when_ready(
    MultiplayerSpawner *p_spawner,
    NetwMultiplayer *p_plane
) {
    if (!p_spawner->is_inside_tree() || gd::node_ready(p_spawner)) {
        return;
    }
    const Callable armed
        = callable_mp(p_plane, &NetwMultiplayer::spawner_wrap_when_ready)
              .bind(p_spawner);
    if (p_spawner->is_connected(SIG_READY, armed)) {
        return;
    }
    p_spawner->connect(SIG_READY, armed, Object::CONNECT_ONE_SHOT);
}

void SpawnerCompat::wrap_spawner(Object *p_spawner) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (spawner == nullptr) {
        return;
    }
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Callable current = spawner->get_spawn_function();
    if (!current.is_valid()) {
        wrap_when_ready(spawner, plane);
        return;
    }
    if (current.get_object() == plane) {
        return;
    }
    originals[int64_t(gd::instance_id(spawner))] = current;
    spawner->set_spawn_function(
        callable_mp(plane, &NetwMultiplayer::spawner_wrapped_spawn)
            .bind(current)
    );
}

Node *SpawnerCompat::wrapped_spawn(
    const Variant &p_data,
    const Callable &p_original
) {
    if (!p_original.is_valid()) {
        return nullptr;
    }
    const Variant built = p_original.call(p_data);
    Node *node = Object::cast_to<Node>(gd::live_object(built));
    if (node != nullptr) {
        custom_args[int64_t(gd::instance_id(node))] = duplicate_arg(p_data);
    }
    return node;
}

Node *SpawnerCompat::instantiate(
    Object *p_spawner,
    int p_scene_index,
    const Variant &p_data
) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (spawner == nullptr) {
        return nullptr;
    }
    const int limit = int(spawner->get_spawn_limit());
    if (limit > 0 && roster.produced_count(spawner) >= limit) {
        NETW_WARN(
            sys::SPAWN,
            "spawn limit reached for '%s'",
            spawner->get_name()
        );
        return nullptr;
    }
    if (p_scene_index >= 0) {
        if (p_scene_index >= int(spawner->get_spawnable_scene_count())) {
            return nullptr;
        }
        const Ref<PackedScene> packed
            = gd::load_scene(spawner->get_spawnable_scene(p_scene_index));
        return packed.is_valid() ? packed->instantiate() : nullptr;
    }
    const HashMap<int64_t, Callable>::Iterator found
        = originals.find(int64_t(gd::instance_id(spawner)));
    const Callable original
        = found ? found->value : spawner->get_spawn_function();
    if (!original.is_valid()) {
        return nullptr;
    }
    const Variant built = original.call(p_data);
    return Object::cast_to<Node>(gd::live_object(built));
}

void SpawnerCompat::note_recv(int64_t p_route, Object *p_spawner) {
    if (Object::cast_to<MultiplayerSpawner>(p_spawner) != nullptr) {
        roster.note_producer(p_route, p_spawner);
    }
}

void SpawnerCompat::emit_spawned(Object *p_spawner, Node *p_node) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(p_spawner);
    if (spawner != nullptr && p_node != nullptr) {
        spawner->emit_signal(SIG_SPAWNED, p_node);
    }
}

void SpawnerCompat::emit_despawned(int64_t p_route, Node *p_node) {
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(roster.take_producer(p_route));
    if (spawner != nullptr && p_node != nullptr) {
        spawner->emit_signal(SIG_DESPAWNED, p_node);
    }
}

void SpawnerCompat::on_session_entered() {
    Node *root = session_root();
    if (root == nullptr) {
        return;
    }
    const TypedArray<Node> found
        = root->find_children("*", "MultiplayerSpawner", true, false);
    for (int at = 0; at < found.size(); ++at) {
        const Variant entry = found[at];
        register_spawner(gd::live_object(entry));
    }
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr) {
        return;
    }
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Callable watcher
        = callable_mp(plane, &NetwMultiplayer::spawner_node_added);
    if (!tree->is_connected(SIG_NODE_ADDED, watcher)) {
        tree->connect(SIG_NODE_ADDED, watcher);
    }
}

void SpawnerCompat::on_node_added(Node *p_node) {
    if (Object::cast_to<MultiplayerSpawner>(p_node) == nullptr) {
        return;
    }
    Node *root = session_root();
    if (root != nullptr && root->is_ancestor_of(p_node)) {
        register_spawner(p_node);
    }
}

void SpawnerCompat::on_session_ended() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    SceneTree *tree = gd::scene_tree();
    const Callable watcher
        = callable_mp(plane, &NetwMultiplayer::spawner_node_added);
    if (tree != nullptr && tree->is_connected(SIG_NODE_ADDED, watcher)) {
        tree->disconnect(SIG_NODE_ADDED, watcher);
    }
    plane->session_defer(
        callable_mp(plane, &NetwMultiplayer::spawner_clear_session_state),
        StringName(KEY_CLEAR_SESSION)
    );
}

void SpawnerCompat::clear_session_state() {
    for (const KeyValue<int64_t, Callable> &row : originals) {
        MultiplayerSpawner *spawner = Object::cast_to<MultiplayerSpawner>(
            gd::instance_from_id(ObjectID(uint64_t(row.key)))
        );
        if (spawner != nullptr) {
            spawner->set_spawn_function(row.value);
        }
    }
    custom_args.clear();
    originals.clear();
    roster.clear();
}

Dictionary SpawnerCompat::counters() const {
    Dictionary out;
    out[StringName("drops_uncaptured_custom")] = drops_uncaptured_custom;
    return out;
}

} // namespace netw::spawn

namespace netw {

spawn::SpawnerCompat *NetwMultiplayer::spawner_adapter() const {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->get_spawner_compat() : nullptr;
}

void NetwMultiplayer::spawner_session_entered() {
    if (spawn::SpawnerCompat *adapter = spawner_adapter()) {
        adapter->on_session_entered();
    }
}

void NetwMultiplayer::spawner_session_ended() {
    if (spawn::SpawnerCompat *adapter = spawner_adapter()) {
        adapter->on_session_ended();
    }
}

void NetwMultiplayer::spawner_node_added(Node *p_node) {
    if (spawn::SpawnerCompat *adapter = spawner_adapter()) {
        adapter->on_node_added(p_node);
    }
}

void NetwMultiplayer::spawner_wrap_when_ready(Node *p_spawner) {
    if (spawn::SpawnerCompat *adapter = spawner_adapter()) {
        adapter->wrap_spawner(p_spawner);
    }
}

void NetwMultiplayer::spawner_clear_session_state() {
    if (spawn::SpawnerCompat *adapter = spawner_adapter()) {
        adapter->clear_session_state();
    }
}

Node *NetwMultiplayer::spawner_wrapped_spawn(
    const Variant &p_data,
    const Callable &p_original
) {
    spawn::SpawnerCompat *adapter = spawner_adapter();
    return adapter != nullptr ? adapter->wrapped_spawn(p_data, p_original)
                              : nullptr;
}

} // namespace netw
