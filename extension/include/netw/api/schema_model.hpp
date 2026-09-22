#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"

namespace netw {

class NetwSchema;

class NetwColumnRef : public godot::RefCounted {
    GDCLASS(NetwColumnRef, godot::RefCounted)

    godot::Ref<NetwSchema> schema;
    int index = -1;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwColumnRef> make(
        const godot::Ref<NetwSchema> &p_schema,
        int p_index
    );

    godot::Ref<NetwSchema> get_schema() const {
        return schema;
    }
    int get_index() const {
        return index;
    }
    godot::StringName get_key() const;
    bool is_valid() const;
};

class NetwSchemaColumn : public godot::RefCounted {
    GDCLASS(NetwSchemaColumn, godot::RefCounted)

    godot::StringName key;
    NetwMultiplayer::ColumnType type = NetwMultiplayer::COLUMN_F32;
    int stride = 1;
    godot::Ref<NetwQuantize> quantizer;

protected:
    static void _bind_methods();

public:
    void set_key(const godot::StringName &p_key) {
        key = p_key;
    }
    godot::StringName get_key() const {
        return key;
    }

    void set_type(NetwMultiplayer::ColumnType p_type) {
        type = p_type;
    }
    NetwMultiplayer::ColumnType get_type() const {
        return type;
    }

    void set_stride(int p_stride) {
        stride = p_stride;
    }
    int get_stride() const {
        return stride;
    }

    void set_quantizer(const godot::Ref<NetwQuantize> &p_quantizer) {
        quantizer = p_quantizer;
    }
    godot::Ref<NetwQuantize> get_quantizer() const {
        return quantizer;
    }
};

class NetwSchema : public godot::RefCounted {
    GDCLASS(NetwSchema, godot::RefCounted)

    godot::StringName name;
    godot::TypedArray<NetwSchemaColumn> columns;
    godot::Dictionary migrations;
    int version = 1;
    bool replicated_lane = true;
    bool reliable_lane = false;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwSchema> create(const godot::StringName &p_name);
    static godot::Ref<NetwSchema> declare(const godot::StringName &p_name);

    godot::StringName get_schema_name() const {
        return name;
    }
    godot::TypedArray<NetwSchemaColumn> get_columns() const {
        return columns;
    }
    bool is_replicated() const {
        return replicated_lane;
    }
    bool is_reliable() const {
        return reliable_lane;
    }
    int get_storage_version() const {
        return version;
    }
    godot::Dictionary get_migrations() const {
        return migrations;
    }

    int column(
        const godot::StringName &p_key,
        NetwMultiplayer::ColumnType p_type,
        int p_stride,
        const godot::Ref<NetwQuantize> &p_quantizer
    );

    int f32(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int f64(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int i8(const godot::StringName &p_key, int p_stride);
    int u8(const godot::StringName &p_key, int p_stride);
    int i16(const godot::StringName &p_key, int p_stride);
    int u16(const godot::StringName &p_key, int p_stride);
    int i32(const godot::StringName &p_key, int p_stride);
    int i64(const godot::StringName &p_key, int p_stride);
    int boolean(const godot::StringName &p_key, int p_stride);
    int vector2(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int vector3(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int vector4(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int color(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int quaternion(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_stride
    );
    int entity(const godot::StringName &p_key, int p_stride);
    int variant(const godot::StringName &p_key, int p_stride);
    int string(const godot::StringName &p_key, int p_stride);

    godot::Ref<NetwColumnRef> column_ref(int p_index);

    godot::Ref<NetwSchema> register_declaration();
    godot::Ref<NetwSchema> replicated(bool p_value);
    godot::Ref<NetwSchema> reliable(bool p_value);
    godot::Ref<NetwSchema> storage_version(int p_version);
    godot::Ref<NetwSchema> migrate(
        int p_from_version,
        const godot::Callable &p_step
    );
};

} // namespace netw
