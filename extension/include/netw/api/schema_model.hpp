#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"

namespace netw {

class NetwSchema : public godot::RefCounted {
    GDCLASS(NetwSchema, godot::RefCounted)

public:
    struct Column {
        godot::StringName key;
        NetwMultiplayer::ColumnType type = NetwMultiplayer::COLUMN_F32;
        int stride = 1;
        godot::Ref<NetwQuantize> quantizer;
    };

private:
    godot::StringName name;
    godot::LocalVector<Column> columns;
    godot::Dictionary migrations;
    int version = 1;
    bool replicated_lane = true;
    bool reliable_lane = false;
    bool read_only = false;

    bool can_edit() const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwSchema> create(const godot::StringName &p_name);
    static godot::Ref<NetwSchema> declare(const godot::StringName &p_name);
    static godot::Ref<NetwSchema> from_record(
        const table::SchemaRecord &p_record
    );

    godot::StringName get_schema_name() const {
        return name;
    }
    const godot::LocalVector<Column> &columns_of() const {
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
