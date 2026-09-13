#pragma once

#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/table/schema_record.hpp"

namespace netw {

using table::SchemaColumn;
using table::SchemaRecord;

class SchemaCore {
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
    static constexpr int MAX_COLUMNS = 64;

private:
    godot::HashMap<godot::RID, SchemaRecord> schemas;
    godot::HashMap<godot::StringName, godot::RID> by_name;

    static int match_redeclared(
        SchemaRecord *record,
        const godot::StringName &key,
        int type,
        int stride
    );

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

    SchemaRecord *record_of(const godot::RID &schema);
    const SchemaRecord *record_of(const godot::RID &schema) const;

    void sealed_records(godot::LocalVector<const SchemaRecord *> &r_out) const;

    static void open_redeclare(SchemaRecord *record);
    static int append_column(
        SchemaRecord *record,
        const godot::StringName &key,
        int type,
        int stride
    );
    static void assign_quantizer(
        SchemaRecord *record,
        int column,
        const godot::Ref<NetwQuantize> &quantizer
    );
    static godot::Error fix(SchemaRecord *record);
    static const SchemaColumn *column_at(
        const SchemaRecord *record,
        int column
    );

    static int compute_hash(const SchemaRecord *record);
    static godot::String quantizer_tag(const SchemaColumn *column);
    static int type_from_variant(int variant_type);
    static godot::Variant make_storage(int type);

    static int storage_type(int type);
    static int element_type(int type);
};

} // namespace netw
