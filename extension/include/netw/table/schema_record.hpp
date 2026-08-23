#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"

namespace netw {

class SchemaColumn : public godot::RefCounted {
    GDCLASS(SchemaColumn, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::StringName key;
    int type = 15;
    int stride = 1;
    godot::Ref<NetwQuantize> quantizer;

    static godot::Ref<SchemaColumn> create(
        const godot::StringName &key,
        int type,
        int stride
    );

    void set_key(const godot::StringName &value);
    godot::StringName get_key() const;
    void set_type(int value);
    int get_type() const;
    void set_stride(int value);
    int get_stride() const;
    void set_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_quantizer() const;
};

class SchemaRecord : public godot::RefCounted {
    GDCLASS(SchemaRecord, godot::RefCounted)

    friend class SchemaCore;

    int redeclare_at = 0;
    bool redeclare_open = false;
    bool redeclare_failed = false;

protected:
    static void _bind_methods();

public:
    godot::StringName name;
    godot::TypedArray<SchemaColumn> columns;
    bool sealed = false;
    int shape_hash = 0;

    godot::Ref<SchemaColumn> at(int column) const;
    int column_count() const;

    void set_name(const godot::StringName &value);
    godot::StringName get_name() const;
    void set_columns(const godot::TypedArray<SchemaColumn> &value);
    godot::TypedArray<SchemaColumn> get_columns() const;
    void set_sealed(bool value);
    bool get_sealed() const;
    void set_shape_hash(int value);
    int get_shape_hash() const;
};

} // namespace netw
