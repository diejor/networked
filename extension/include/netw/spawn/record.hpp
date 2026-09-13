#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::spawn {

struct VerbHead {
    uint64_t route = 0;
    uint64_t epoch = 0;

    static constexpr auto wire = wire::describe(
        wire::field<&VerbHead::route>("route", wire::varuint(5)),
        wire::field<&VerbHead::epoch>("epoch", wire::varuint(3))
    );
};

struct DescriptorRow {
    uint64_t ordinal = 0;
    uint64_t schema_hash = 0;

    static constexpr auto wire = wire::describe(
        wire::field<&DescriptorRow::ordinal>("ordinal", wire::varuint(5)),
        wire::field<&DescriptorRow::schema_hash>("schema_hash", wire::bits(32))
    );
};

godot::Dictionary verb_spec_records();

class Record {
private:
    godot::ObjectID node_id;
    godot::ObjectID fn_host_id;
    godot::ObjectID spawner_id;
    godot::LocalVector<int32_t> recipient_peers;

    int64_t route = 0;
    godot::StringName entity_id;
    int64_t peer_id = 0;
    int64_t controller = 0;
    int64_t parent_route = 0;
    int32_t recipe = 0;
    int32_t scene_index = -1;
    godot::String scene_path;
    godot::String node_name;
    godot::StringName fn_method;
    godot::StringName fn_registry_id;
    godot::Array fn_args;
    godot::Variant custom_data;

public:
    bool encode_header(
        wire::WriteStream &p_stream,
        godot::Object *p_entity
    ) const;

    static bool decode_header(
        wire::ReadStream &p_stream,
        godot::Dictionary &r_header
    );

    static godot::Ref<NetwEntity> stamp_header(
        godot::Object *p_node,
        const godot::Dictionary &p_header,
        godot::Object *p_session
    );

    static void apply_native_entry(
        godot::Node *p_root,
        const godot::String &p_path,
        const godot::PackedByteArray &p_bytes
    );

    static godot::PackedByteArray write_descriptors(
        const godot::LocalVector<DescriptorRow> &p_rows
    );

    static bool read_descriptors(
        wire::ReadStream &p_stream,
        godot::Dictionary &r_descriptors
    );

    static godot::Dictionary descriptors_of_bytes(
        const godot::PackedByteArray &p_bytes
    );

    godot::Node *node() const;
    void bind_node(godot::Node *p_node);

    godot::Node *fn_host() const;
    void bind_fn_host(godot::Node *p_host);

    godot::MultiplayerSpawner *spawner() const;
    void bind_spawner(godot::MultiplayerSpawner *p_spawner);

    bool add_recipient(int peer);
    bool remove_recipient(int peer);
    bool has_recipient(int peer) const;
    godot::PackedInt32Array recipients() const;
    void set_recipients(const godot::PackedInt32Array &peers);

    void set_route(int64_t value) {
        route = value;
    }
    int64_t get_route() const {
        return route;
    }
    void set_entity_id(const godot::StringName &value) {
        entity_id = value;
    }
    godot::StringName get_entity_id() const {
        return entity_id;
    }
    void set_peer_id(int64_t value) {
        peer_id = value;
    }
    int64_t get_peer_id() const {
        return peer_id;
    }
    void set_controller(int64_t value) {
        controller = value;
    }
    int64_t get_controller() const {
        return controller;
    }
    void set_parent_route(int64_t value) {
        parent_route = value;
    }
    int64_t get_parent_route() const {
        return parent_route;
    }
    void set_recipe(int value) {
        recipe = value;
    }
    int get_recipe() const {
        return recipe;
    }
    void set_scene_index(int value) {
        scene_index = value;
    }
    int get_scene_index() const {
        return scene_index;
    }
    void set_scene_path(const godot::String &value) {
        scene_path = value;
    }
    godot::String get_scene_path() const {
        return scene_path;
    }
    void set_node_name(const godot::String &value) {
        node_name = value;
    }
    godot::String get_node_name() const {
        return node_name;
    }
    void set_fn_method(const godot::StringName &value) {
        fn_method = value;
    }
    godot::StringName get_fn_method() const {
        return fn_method;
    }
    void set_fn_registry_id(const godot::StringName &value) {
        fn_registry_id = value;
    }
    godot::StringName get_fn_registry_id() const {
        return fn_registry_id;
    }
    void set_fn_args(const godot::Array &value) {
        fn_args = value;
    }
    godot::Array get_fn_args() const {
        return fn_args;
    }
    void set_custom_data(const godot::Variant &value) {
        custom_data = value;
    }
    godot::Variant get_custom_data() const {
        return custom_data;
    }
};

} // namespace netw::spawn
