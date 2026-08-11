#pragma once

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/quantize.hpp"

namespace netw {

// One declared column. Declaration order is address order, so a column's index
// in its record is what every binding addresses it by.
class SchemaColumn : public godot::RefCounted {
    GDCLASS(SchemaColumn, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::StringName key;
    // One of SchemaCore::ColumnType. Held as int because a property set
    // rewrites it while compiling a reflected script property.
    int type = 15;
    int stride = 1;
    godot::Ref<NetwQuantize> quantizer;

    // A registered class takes no constructor arguments, so the three-value
    // form the schema builds columns through is a bound static.
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

// One schema's declaration, the whole of what two peers must agree on. Fixed
// by SchemaCore::fix and never changed again, because a peer that sealed a
// different order would address a different column by the same index.
class SchemaRecord : public godot::RefCounted {
    GDCLASS(SchemaRecord, godot::RefCounted)

    friend class SchemaCore;

    // Cursor and outcome of an idempotent re-declaration against a sealed
    // record. Open only between a repeat declare and the seal that closes it.
    int redeclare_at = 0;
    bool redeclare_open = false;
    bool redeclare_failed = false;

protected:
    static void _bind_methods();

public:
    godot::StringName name;
    // Shared with GDScript rather than copied: a property set appends its own
    // compiled column shapes into this array.
    godot::TypedArray<SchemaColumn> columns;
    bool sealed = false;
    int shape_hash = 0;

    // The declared column at an index, or null when the address is invalid.
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

// The ordered, typed column list two peers must agree on, and nothing else. It
// exists once so the row-major property binding, the column-major table
// binding, and the database read the same column list instead of three that
// drift.
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

    // One past the last declared shape, so a table sized by the enum stays
    // indexable by the whole of it.
    static constexpr int COLUMN_TYPE_COUNT = 16;

private:
    godot::HashMap<godot::RID, godot::Ref<SchemaRecord>> schemas;
    // name -> the RID a session minted for it, so find() is a lookup rather
    // than a scan and a repeat declare reaches the record it already has.
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
    // Declaration, keyed by the RID its owning session minted.
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

    // Reflection. Every verb answers a handle that names no record rather than
    // reaching into a null.
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

    // The record a handle names, the one door a binding reaches a declaration
    // through.
    godot::Ref<SchemaRecord> record_of(const godot::RID &schema) const;

    // The record-facing halves, for a declaration with no session to key it.
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

    // Shape. The formula is the contract, not the implementation: changing it
    // changes the bytes on the wire.
    static int compute_hash(const godot::Ref<SchemaRecord> &record);
    static godot::String quantizer_tag(const godot::Ref<SchemaColumn> &column);
    static int type_from_variant(int variant_type);
    static godot::Variant make_storage(int type);

    // The two per-type tables, as accessors rather than as constant arrays:
    // ClassDB binds integers, and an array constant has no spelling.
    static int storage_type(int type);
    static int element_type(int type);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::SchemaCore::ColumnType);
