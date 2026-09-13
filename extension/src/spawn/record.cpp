#include "netw/spawn/record.hpp"

#include "godot/callable.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw::spawn {

namespace {

template <class T> T *resolve(const ObjectID &p_id) {
    if (!p_id.is_valid()) {
        return nullptr;
    }
    return Object::cast_to<T>(gd::instance_from_id(p_id));
}

} // namespace

bool Record::encode_header(
    wire::WriteStream &p_stream,
    Object *p_entity
) const {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_entity);
    String id = String(entity_id);
    uint64_t peer = uint64_t(peer_id > 0 ? peer_id : 0);
    int64_t control = entity != nullptr ? entity->get_controller() : controller;
    uint64_t spawn_tick = uint64_t(
        (entity != nullptr ? entity->get_action_spawn_tick() : int64_t(-1)) + 1
    );
    int64_t requester
        = entity != nullptr ? entity->get_action_requester() : int64_t(0);
    uint64_t comp_hash = uint64_t(uint32_t(
        entity != nullptr ? entity->comp_table().get_wire_hash() : int64_t(0)
    ));
    bool declares_scene = entity != nullptr && entity->get_declares_scene();
    String scene_label
        = declares_scene ? String(entity->get_scene_label()) : String();
    String name = node_name;

    if (!wire::string_field(p_stream, id) || !p_stream.varuint(peer, 5)
        || !p_stream.svarint(control, 5) || !p_stream.varuint(spawn_tick, 5)
        || !p_stream.svarint(requester, 5) || !p_stream.bits(comp_hash, 32)
        || !p_stream.bool1(declares_scene)) {
        return false;
    }
    if (declares_scene && !wire::string_field(p_stream, scene_label)) {
        return false;
    }
    return wire::string_field(p_stream, name);
}

Ref<NetwEntity> Record::stamp_header(
    Object *p_node,
    const Dictionary &p_header,
    Object *p_session
) {
    const Ref<NetwEntity> entity
        = NetwEntity::ensure(Object::cast_to<Node>(p_node));
    if (entity.is_null()) {
        return entity;
    }
    entity->set_entity_id(p_header.get(StringName("entity_id"), StringName()));
    entity->set_peer_id(int64_t(p_header.get(StringName("peer_id"), 0)));
    entity->set_controller(int64_t(p_header.get(StringName("controller"), 0)));
    entity->set_action_spawn_tick(
        int64_t(p_header.get(StringName("action_spawn_tick"), -1))
    );
    entity->set_action_requester(
        int64_t(p_header.get(StringName("action_requester"), 0))
    );
    entity->comp_table().set_wire_hash(
        int64_t(p_header.get(StringName("wire_hash"), 0))
    );
    entity->set_declares_scene(
        bool(p_header.get(StringName("declares_scene"), false))
    );
    entity->set_scene_label(
        p_header.get(StringName("scene_label"), StringName())
    );
    entity->set_route(int64_t(p_header.get(StringName("route"), 0)));
    if (entity->get_stage() == int64_t(netw::entity::Stage::UNBOUND)) {
        entity->arm(Object::cast_to<NetwMultiplayer>(p_session));
    }
    return entity;
}

Dictionary verb_spec_records() {
    Dictionary out;
    out["SpawnVerbHead"] = VerbHead::wire.spec_dump();
    out["SpawnDescriptorRow"] = DescriptorRow::wire.spec_dump();
    return out;
}

void Record::apply_native_entry(
    Node *p_root,
    const String &p_path,
    const PackedByteArray &p_bytes
) {
    const NodePath path = NodePath(p_path);
    Node *target = p_root;
    const NodePath names = NodePath(path.get_concatenated_names());
    if (target != nullptr && !names.is_empty()) {
        target = target->get_node_or_null(names);
    }
    if (target == nullptr) {
        return;
    }
    gd::set_indexed(
        target,
        NodePath(":" + path.get_concatenated_subnames()),
        gd::bytes_to_var(p_bytes)
    );
}

PackedByteArray Record::write_descriptors(
    const LocalVector<DescriptorRow> &p_rows
) {
    wire::WriteStream stream;
    uint64_t count = uint64_t(p_rows.size());
    if (!stream.varuint(count, 2)) {
        return PackedByteArray();
    }
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        DescriptorRow staged = p_rows[at];
        if (!DescriptorRow::wire.run(stream, staged)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool Record::read_descriptors(
    wire::ReadStream &p_stream,
    Dictionary &r_descriptors
) {
    uint64_t count = 0;
    if (!p_stream.varuint(count, 2)) {
        return false;
    }
    Dictionary staged;
    for (uint64_t at = 0; at < count; ++at) {
        DescriptorRow row;
        if (!DescriptorRow::wire.run(p_stream, row)) {
            return false;
        }
        staged[int64_t(row.ordinal)] = int64_t(row.schema_hash);
    }
    r_descriptors = staged;
    return true;
}

Dictionary Record::descriptors_of_bytes(const PackedByteArray &p_bytes) {
    wire::ReadStream stream(p_bytes);
    Dictionary out;
    if (!read_descriptors(stream, out) || !stream.align_verify()
        || stream.bits_remaining() != 0) {
        return Dictionary();
    }
    return out;
}

bool Record::decode_header(wire::ReadStream &p_stream, Dictionary &r_header) {
    String id;
    uint64_t peer = 0;
    int64_t control = 0;
    uint64_t spawn_tick = 0;
    int64_t requester = 0;
    uint64_t comp_hash = 0;
    bool declares_scene = false;
    String scene_label;
    String name;

    if (!wire::string_field(p_stream, id) || !p_stream.varuint(peer, 5)
        || !p_stream.svarint(control, 5) || !p_stream.varuint(spawn_tick, 5)
        || !p_stream.svarint(requester, 5) || !p_stream.bits(comp_hash, 32)
        || !p_stream.bool1(declares_scene)) {
        return false;
    }
    if (declares_scene && !wire::string_field(p_stream, scene_label)) {
        return false;
    }
    if (!wire::string_field(p_stream, name)) {
        return false;
    }

    r_header[StringName("entity_id")] = StringName(id);
    r_header[StringName("peer_id")] = int64_t(peer);
    r_header[StringName("controller")] = control;
    r_header[StringName("action_spawn_tick")] = int64_t(spawn_tick) - 1;
    r_header[StringName("action_requester")] = requester;
    r_header[StringName("wire_hash")] = int64_t(comp_hash);
    r_header[StringName("declares_scene")] = declares_scene;
    r_header[StringName("scene_label")] = StringName(scene_label);
    r_header[StringName("node_name")] = name;
    return true;
}

Node *Record::node() const {
    return resolve<Node>(node_id);
}

void Record::bind_node(Node *p_node) {
    node_id = gd::instance_id(p_node);
}

Node *Record::fn_host() const {
    return resolve<Node>(fn_host_id);
}

void Record::bind_fn_host(Node *p_host) {
    fn_host_id = gd::instance_id(p_host);
}

MultiplayerSpawner *Record::spawner() const {
    return resolve<MultiplayerSpawner>(spawner_id);
}

void Record::bind_spawner(MultiplayerSpawner *p_spawner) {
    spawner_id = gd::instance_id(p_spawner);
}

bool Record::add_recipient(int peer) {
    if (has_recipient(peer)) {
        return false;
    }
    recipient_peers.push_back(int32_t(peer));
    return true;
}

bool Record::remove_recipient(int peer) {
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        if (recipient_peers[index] == int32_t(peer)) {
            recipient_peers.remove_at(index);
            return true;
        }
    }
    return false;
}

bool Record::has_recipient(int peer) const {
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        if (recipient_peers[index] == int32_t(peer)) {
            return true;
        }
    }
    return false;
}

PackedInt32Array Record::recipients() const {
    PackedInt32Array out;
    out.resize(int64_t(recipient_peers.size()));
    for (uint32_t index = 0; index < recipient_peers.size(); ++index) {
        out.set(int64_t(index), recipient_peers[index]);
    }
    return out;
}

void Record::set_recipients(const PackedInt32Array &peers) {
    recipient_peers.clear();
    for (int64_t index = 0; index < peers.size(); ++index) {
        add_recipient(peers[index]);
    }
}

} // namespace netw::spawn
