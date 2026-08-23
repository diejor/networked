#include "netw/api/spawn_record.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

template <class T> T *resolve(const ObjectID &p_id) {
    if (!p_id.is_valid()) {
        return nullptr;
    }
    return Object::cast_to<T>(gd::instance_from_id(p_id));
}

} // namespace

Node *NetwSpawnRecord::node() const {
    return resolve<Node>(node_id);
}

void NetwSpawnRecord::bind_node(Node *p_node) {
    node_id = gd::instance_id(p_node);
}

Node *NetwSpawnRecord::fn_host() const {
    return resolve<Node>(fn_host_id);
}

void NetwSpawnRecord::bind_fn_host(Node *p_host) {
    fn_host_id = gd::instance_id(p_host);
}

MultiplayerSpawner *NetwSpawnRecord::spawner() const {
    return resolve<MultiplayerSpawner>(spawner_id);
}

void NetwSpawnRecord::bind_spawner(MultiplayerSpawner *p_spawner) {
    spawner_id = gd::instance_id(p_spawner);
}

bool NetwSpawnRecord::add_recipient(int peer) {
    if (has_recipient(peer)) {
        return false;
    }
    recipient_peers.push_back(int32_t(peer));
    return true;
}

bool NetwSpawnRecord::remove_recipient(int peer) {
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        if (recipient_peers[index] == int32_t(peer)) {
            recipient_peers.remove_at(index);
            return true;
        }
    }
    return false;
}

bool NetwSpawnRecord::has_recipient(int peer) const {
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        if (recipient_peers[index] == int32_t(peer)) {
            return true;
        }
    }
    return false;
}

PackedInt32Array NetwSpawnRecord::recipients() const {
    PackedInt32Array out;
    out.resize(int64_t(recipient_peers.size()));
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        out.set(int64_t(index), recipient_peers[index]);
    }
    return out;
}

void NetwSpawnRecord::set_recipients(const PackedInt32Array &peers) {
    recipient_peers.clear();
    for (int64_t index = 0; index < peers.size(); ++index) {
        add_recipient(peers[index]);
    }
}

#define NETW_SPAWN_FIELD(m_type, m_name)                                       \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwSpawnRecord::get_##m_name                                         \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwSpawnRecord::set_##m_name                                         \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

void NetwSpawnRecord::_bind_methods() {
    ClassDB::bind_method(D_METHOD("node"), &NetwSpawnRecord::node);
    ClassDB::bind_method(
        D_METHOD("bind_node", "node"),
        &NetwSpawnRecord::bind_node
    );
    ClassDB::bind_method(D_METHOD("fn_host"), &NetwSpawnRecord::fn_host);
    ClassDB::bind_method(
        D_METHOD("bind_fn_host", "host"),
        &NetwSpawnRecord::bind_fn_host
    );
    ClassDB::bind_method(D_METHOD("spawner"), &NetwSpawnRecord::spawner);
    ClassDB::bind_method(
        D_METHOD("bind_spawner", "spawner"),
        &NetwSpawnRecord::bind_spawner
    );

    ClassDB::bind_method(
        D_METHOD("add_recipient", "peer"),
        &NetwSpawnRecord::add_recipient
    );
    ClassDB::bind_method(
        D_METHOD("remove_recipient", "peer"),
        &NetwSpawnRecord::remove_recipient
    );
    ClassDB::bind_method(
        D_METHOD("has_recipient", "peer"),
        &NetwSpawnRecord::has_recipient
    );
    ClassDB::bind_method(
        D_METHOD("recipients"),
        &NetwSpawnRecord::recipients
    );
    ClassDB::bind_method(
        D_METHOD("set_recipients", "peers"),
        &NetwSpawnRecord::set_recipients
    );

    NETW_SPAWN_FIELD(Variant::INT, route);
    NETW_SPAWN_FIELD(Variant::STRING_NAME, entity_id);
    NETW_SPAWN_FIELD(Variant::INT, peer_id);
    NETW_SPAWN_FIELD(Variant::INT, controller);
    NETW_SPAWN_FIELD(Variant::INT, parent_route);
    NETW_SPAWN_FIELD(Variant::INT, recipe);
    NETW_SPAWN_FIELD(Variant::INT, scene_index);
    NETW_SPAWN_FIELD(Variant::STRING, scene_path);
    NETW_SPAWN_FIELD(Variant::STRING, node_name);
    NETW_SPAWN_FIELD(Variant::STRING_NAME, fn_method);
    NETW_SPAWN_FIELD(Variant::STRING_NAME, fn_registry_id);
    NETW_SPAWN_FIELD(Variant::ARRAY, fn_args);
    ClassDB::bind_method(
        D_METHOD("get_custom_data"),
        &NetwSpawnRecord::get_custom_data
    );
    ClassDB::bind_method(
        D_METHOD("set_custom_data", "custom_data"),
        &NetwSpawnRecord::set_custom_data
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "custom_data",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "set_custom_data",
        "get_custom_data"
    );
}

} // namespace netw
