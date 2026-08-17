#pragma once

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/quantize.hpp"

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

class SchemaCore : public godot::RefCounted {
    GDCLASS(SchemaCore, godot::RefCounted)

public:
    enum ColumnType {
        F32 = 0,
        F64 = 1,
        I8 = 2,
        U8 = 3,
        I16 = 4,
        U16 = 5,
        I32 = 6,
        I64 = 7,
        BOOL = 8,
        VECTOR2 = 9,
        VECTOR3 = 10,
        VECTOR4 = 11,
        COLOR = 12,
        QUATERNION = 13,
        ENTITY = 14,
        VARIANT = 15,
    };

    static constexpr int COLUMN_TYPE_COUNT = 16;

private:
    godot::HashMap<godot::RID, godot::Ref<SchemaRecord>> schemas;
    godot::HashMap<godot::StringName, godot::RID> by_name;

    static int match_redeclared(
        const godot::Ref<SchemaRecord> &record,
        const godot::StringName &key,
        int type,
        int stride
    );

protected:
    static void _bind_methods();

public:
    void declare(const godot::RID &schema, const godot::StringName &name);
    int add_column(
        const godot::RID &schema,
        const godot::StringName &key,
        int type,
        int stride
    );
    void set_column_quantizer(
        const godot::RID &schema,
        int column,
        const godot::Ref<NetwQuantize> &quantizer
    );
    godot::Error seal(const godot::RID &schema);

    bool is_valid(const godot::RID &schema) const;
    godot::RID find(const godot::StringName &name) const;
    godot::StringName name_of(const godot::RID &schema) const;
    int hash_of(const godot::RID &schema) const;
    int column_count(const godot::RID &schema) const;
    godot::StringName column_key(const godot::RID &schema, int column) const;
    int column_type(const godot::RID &schema, int column) const;
    int column_stride(const godot::RID &schema, int column) const;
    godot::Ref<NetwQuantize> column_quantizer(
        const godot::RID &schema,
        int column
    ) const;
    int find_column(
        const godot::RID &schema,
        const godot::StringName &key
    ) const;
    bool has_variant(const godot::RID &schema) const;
    bool has_stride(const godot::RID &schema) const;

    godot::Ref<SchemaRecord> record_of(const godot::RID &schema) const;

    static void open_redeclare(const godot::Ref<SchemaRecord> &record);
    static int append_column(
        const godot::Ref<SchemaRecord> &record,
        const godot::StringName &key,
        int type,
        int stride
    );
    static void assign_quantizer(
        const godot::Ref<SchemaRecord> &record,
        int column,
        const godot::Ref<NetwQuantize> &quantizer
    );
    static godot::Error fix(const godot::Ref<SchemaRecord> &record);
    static godot::Ref<SchemaColumn> column_at(
        const godot::Ref<SchemaRecord> &record,
        int column
    );

    static int compute_hash(const godot::Ref<SchemaRecord> &record);
    static godot::String quantizer_tag(const godot::Ref<SchemaColumn> &column);
    static int type_from_variant(int variant_type);
    static godot::Variant make_storage(int type);

    static int storage_type(int type);
    static int element_type(int type);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::SchemaCore::ColumnType);
