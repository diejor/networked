#include "netw/api/schema_core.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

const Variant::Type STORAGE_TYPES[SchemaCore::COLUMN_TYPE_COUNT] = {
    Variant::PACKED_FLOAT32_ARRAY,
    Variant::PACKED_FLOAT64_ARRAY,
    Variant::PACKED_INT32_ARRAY,
    Variant::PACKED_INT32_ARRAY,
    Variant::PACKED_INT32_ARRAY,
    Variant::PACKED_INT32_ARRAY,
    Variant::PACKED_INT32_ARRAY,
    Variant::PACKED_INT64_ARRAY,
    Variant::PACKED_BYTE_ARRAY,
    Variant::PACKED_VECTOR2_ARRAY,
    Variant::PACKED_VECTOR3_ARRAY,
    Variant::PACKED_VECTOR4_ARRAY,
    Variant::PACKED_COLOR_ARRAY,
    Variant::PACKED_VECTOR4_ARRAY,
    Variant::PACKED_INT64_ARRAY,
    Variant::ARRAY,
};

const Variant::Type ELEMENT_TYPES[SchemaCore::COLUMN_TYPE_COUNT] = {
    Variant::FLOAT,
    Variant::FLOAT,
    Variant::INT,
    Variant::INT,
    Variant::INT,
    Variant::INT,
    Variant::INT,
    Variant::INT,
    Variant::BOOL,
    Variant::VECTOR2,
    Variant::VECTOR3,
    Variant::VECTOR4,
    Variant::COLOR,
    Variant::QUATERNION,
    Variant::INT,
    Variant::NIL,
};

bool in_range(int type) {
    return type >= 0 && type < SchemaCore::COLUMN_TYPE_COUNT;
}

} // namespace
void SchemaCore::declare(const RID &schema, const StringName &name) {
    Ref<SchemaRecord> existing = record_of(schema);
    if (existing.is_valid()) {
        open_redeclare(existing);
        return;
    }
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = name;
    schemas[schema] = record;
    by_name[name] = schema;
}

int SchemaCore::add_column(
    const RID &schema,
    const StringName &key,
    int type,
    int stride
) {
    return append_column(record_of(schema), key, type, stride);
}

void SchemaCore::set_column_quantizer(
    const RID &schema,
    int column,
    const Ref<NetwQuantize> &quantizer
) {
    assign_quantizer(record_of(schema), column, quantizer);
}

Error SchemaCore::seal(const RID &schema) {
    NETW_ZONE_NC("SchemaCore seal", colors::TABLE);
    Ref<SchemaRecord> record = record_of(schema);
    if (record.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    return fix(record);
}

Ref<SchemaRecord> SchemaCore::record_of(const RID &schema) const {
    const HashMap<RID, Ref<SchemaRecord>>::ConstIterator found
        = schemas.find(schema);
    return found != schemas.end() ? found->value : Ref<SchemaRecord>();
}

bool SchemaCore::is_valid(const RID &schema) const {
    return schemas.has(schema);
}

RID SchemaCore::find(const StringName &name) const {
    const HashMap<StringName, RID>::ConstIterator found = by_name.find(name);
    return found != by_name.end() ? found->value : RID();
}

StringName SchemaCore::name_of(const RID &schema) const {
    const Ref<SchemaRecord> record = record_of(schema);
    return record.is_valid() ? record->name : StringName();
}

int SchemaCore::hash_of(const RID &schema) const {
    const Ref<SchemaRecord> record = record_of(schema);
    return record.is_valid() ? record->shape_hash : 0;
}

int SchemaCore::column_count(const RID &schema) const {
    const Ref<SchemaRecord> record = record_of(schema);
    return record.is_valid() ? record->column_count() : 0;
}

StringName SchemaCore::column_key(const RID &schema, int column) const {
    const Ref<SchemaColumn> found = column_at(record_of(schema), column);
    return found.is_valid() ? found->key : StringName();
}

int SchemaCore::column_type(const RID &schema, int column) const {
    const Ref<SchemaColumn> found = column_at(record_of(schema), column);
    return found.is_valid() ? found->type : -1;
}

int SchemaCore::column_stride(const RID &schema, int column) const {
    const Ref<SchemaColumn> found = column_at(record_of(schema), column);
    return found.is_valid() ? found->stride : 0;
}

Ref<NetwQuantize> SchemaCore::column_quantizer(
    const RID &schema,
    int column
) const {
    const Ref<SchemaColumn> found = column_at(record_of(schema), column);
    return found.is_valid() ? found->quantizer : Ref<NetwQuantize>();
}

int SchemaCore::find_column(const RID &schema, const StringName &key) const {
    const Ref<SchemaRecord> record = record_of(schema);
    if (record.is_null()) {
        return -1;
    }
    for (int i = 0; i < record->column_count(); i++) {
        const Ref<SchemaColumn> column = record->at(i);
        if (column.is_valid() && column->key == key) {
            return i;
        }
    }
    return -1;
}

bool SchemaCore::has_variant(const RID &schema) const {
    const Ref<SchemaRecord> record = record_of(schema);
    if (record.is_null()) {
        return false;
    }
    for (int i = 0; i < record->column_count(); i++) {
        const Ref<SchemaColumn> column = record->at(i);
        if (column.is_valid() && column->type == VARIANT) {
            return true;
        }
    }
    return false;
}

bool SchemaCore::has_stride(const RID &schema) const {
    const Ref<SchemaRecord> record = record_of(schema);
    if (record.is_null()) {
        return false;
    }
    for (int i = 0; i < record->column_count(); i++) {
        const Ref<SchemaColumn> column = record->at(i);
        if (column.is_valid() && column->stride > 1) {
            return true;
        }
    }
    return false;
}

void SchemaCore::open_redeclare(const Ref<SchemaRecord> &record) {
    if (record.is_null()) {
        return;
    }
    record->redeclare_at = 0;
    record->redeclare_open = record->sealed;
    record->redeclare_failed = false;
}

int SchemaCore::append_column(
    const Ref<SchemaRecord> &record,
    const StringName &key,
    int type,
    int stride
) {
    if (record.is_null() || key == StringName() || stride < 1) {
        return -1;
    }
    if (!in_range(type)) {
        return -1;
    }
    if (record->sealed) {
        return match_redeclared(record, key, type, stride);
    }
    for (int i = 0; i < record->column_count(); i++) {
        const Ref<SchemaColumn> column = record->at(i);
        if (column.is_valid() && column->key == key) {
            return -1;
        }
    }
    record->columns.push_back(SchemaColumn::create(key, type, stride));
    return record->column_count() - 1;
}

void SchemaCore::assign_quantizer(
    const Ref<SchemaRecord> &record,
    int column,
    const Ref<NetwQuantize> &quantizer
) {
    if (record.is_null() || record->sealed) {
        return;
    }
    const Ref<SchemaColumn> found = record->at(column);
    if (found.is_null()) {
        return;
    }
    found->quantizer = quantizer;
}

Error SchemaCore::fix(const Ref<SchemaRecord> &record) {
    if (record.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    if (record->sealed) {
        if (!record->redeclare_open) {
            return OK;
        }
        const bool matched = !record->redeclare_failed
            && record->redeclare_at == record->column_count();
        record->redeclare_open = false;
        record->redeclare_at = 0;
        record->redeclare_failed = false;
        return matched ? OK : ERR_UNCONFIGURED;
    }
    record->shape_hash = compute_hash(record);
    record->sealed = true;
    return OK;
}

Ref<SchemaColumn> SchemaCore::column_at(
    const Ref<SchemaRecord> &record,
    int column
) {
    return record.is_valid() ? record->at(column) : Ref<SchemaColumn>();
}

int SchemaCore::match_redeclared(
    const Ref<SchemaRecord> &record,
    const StringName &key,
    int type,
    int stride
) {
    if (!record->redeclare_open) {
        return -1;
    }
    const int at = record->redeclare_at;
    if (at >= record->column_count()) {
        record->redeclare_failed = true;
        return -1;
    }
    const Ref<SchemaColumn> column = record->at(at);
    if (column.is_null() || column->key != key || column->type != type
        || column->stride != stride) {
        record->redeclare_failed = true;
        return -1;
    }
    record->redeclare_at = at + 1;
    return at;
}

int SchemaCore::compute_hash(const Ref<SchemaRecord> &record) {
    if (record.is_null()) {
        return 0;
    }
    PackedStringArray parts;
    parts.push_back(String(record->name));
    for (int i = 0; i < record->column_count(); i++) {
        const Ref<SchemaColumn> column = record->at(i);
        if (column.is_null()) {
            continue;
        }
        parts.push_back(
            String(column->key) + ":" + String::num_int64(column->type) + ":"
            + String::num_int64(column->stride) + ":" + quantizer_tag(column)
        );
    }
    const uint32_t folded
        = static_cast<uint32_t>(String("|").join(parts).hash());
    return static_cast<int>(folded & 0xFFFFu);
}

String SchemaCore::quantizer_tag(const Ref<SchemaColumn> &column) {
    if (column.is_null() || column->quantizer.is_null()) {
        return "raw";
    }
    const int element = element_type(column->type);
    return column->quantizer->get_class() + "/"
        + String::num_int64(column->quantizer->bit_width(element));
}

int SchemaCore::type_from_variant(int variant_type) {
    switch (variant_type) {
        case Variant::BOOL:
            return BOOL;
        case Variant::INT:
            return I64;
        case Variant::FLOAT:
            return F64;
        case Variant::VECTOR2:
            return VECTOR2;
        case Variant::VECTOR3:
            return VECTOR3;
        case Variant::VECTOR4:
            return VECTOR4;
        case Variant::COLOR:
            return COLOR;
        case Variant::QUATERNION:
            return QUATERNION;
        default:
            return VARIANT;
    }
}

Variant SchemaCore::make_storage(int type) {
    if (!in_range(type)) {
        return Variant();
    }
    switch (STORAGE_TYPES[type]) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return PackedFloat32Array();
        case Variant::PACKED_FLOAT64_ARRAY:
            return PackedFloat64Array();
        case Variant::PACKED_INT32_ARRAY:
            return PackedInt32Array();
        case Variant::PACKED_INT64_ARRAY:
            return PackedInt64Array();
        case Variant::PACKED_BYTE_ARRAY:
            return PackedByteArray();
        case Variant::PACKED_VECTOR2_ARRAY:
            return PackedVector2Array();
        case Variant::PACKED_VECTOR3_ARRAY:
            return PackedVector3Array();
        case Variant::PACKED_VECTOR4_ARRAY:
            return PackedVector4Array();
        case Variant::PACKED_COLOR_ARRAY:
            return PackedColorArray();
        case Variant::ARRAY:
            return Array();
        default:
            return Variant();
    }
}

int SchemaCore::storage_type(int type) {
    return in_range(type) ? static_cast<int>(STORAGE_TYPES[type]) : -1;
}

int SchemaCore::element_type(int type) {
    return in_range(type) ? static_cast<int>(ELEMENT_TYPES[type]) : -1;
}

void SchemaCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("declare", "schema", "name"),
        &SchemaCore::declare
    );
    ClassDB::bind_method(
        D_METHOD("add_column", "schema", "key", "type", "stride"),
        &SchemaCore::add_column,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("set_column_quantizer", "schema", "column", "quantizer"),
        &SchemaCore::set_column_quantizer
    );
    ClassDB::bind_method(D_METHOD("seal", "schema"), &SchemaCore::seal);
    ClassDB::bind_method(D_METHOD("is_valid", "schema"), &SchemaCore::is_valid);
    ClassDB::bind_method(D_METHOD("find", "name"), &SchemaCore::find);
    ClassDB::bind_method(D_METHOD("name_of", "schema"), &SchemaCore::name_of);
    ClassDB::bind_method(D_METHOD("hash_of", "schema"), &SchemaCore::hash_of);
    ClassDB::bind_method(
        D_METHOD("column_count", "schema"),
        &SchemaCore::column_count
    );
    ClassDB::bind_method(
        D_METHOD("column_key", "schema", "column"),
        &SchemaCore::column_key
    );
    ClassDB::bind_method(
        D_METHOD("column_type", "schema", "column"),
        &SchemaCore::column_type
    );
    ClassDB::bind_method(
        D_METHOD("column_stride", "schema", "column"),
        &SchemaCore::column_stride
    );
    ClassDB::bind_method(
        D_METHOD("column_quantizer", "schema", "column"),
        &SchemaCore::column_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("find_column", "schema", "key"),
        &SchemaCore::find_column
    );
    ClassDB::bind_method(
        D_METHOD("has_variant", "schema"),
        &SchemaCore::has_variant
    );
    ClassDB::bind_method(
        D_METHOD("has_stride", "schema"),
        &SchemaCore::has_stride
    );
    ClassDB::bind_method(
        D_METHOD("record_of", "schema"),
        &SchemaCore::record_of
    );

    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("open_redeclare", "record"),
        &SchemaCore::open_redeclare
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("append_column", "record", "key", "type", "stride"),
        &SchemaCore::append_column,
        DEFVAL(1)
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("assign_quantizer", "record", "column", "quantizer"),
        &SchemaCore::assign_quantizer
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("fix", "record"),
        &SchemaCore::fix
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("column_at", "record", "column"),
        &SchemaCore::column_at
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("compute_hash", "record"),
        &SchemaCore::compute_hash
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("quantizer_tag", "column"),
        &SchemaCore::quantizer_tag
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("type_from_variant", "variant_type"),
        &SchemaCore::type_from_variant
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("make_storage", "type"),
        &SchemaCore::make_storage
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("storage_type", "type"),
        &SchemaCore::storage_type
    );
    ClassDB::bind_static_method(
        "SchemaCore",
        D_METHOD("element_type", "type"),
        &SchemaCore::element_type
    );

    BIND_ENUM_CONSTANT(F32);
    BIND_ENUM_CONSTANT(F64);
    BIND_ENUM_CONSTANT(I8);
    BIND_ENUM_CONSTANT(U8);
    BIND_ENUM_CONSTANT(I16);
    BIND_ENUM_CONSTANT(U16);
    BIND_ENUM_CONSTANT(I32);
    BIND_ENUM_CONSTANT(I64);
    BIND_ENUM_CONSTANT(BOOL);
    BIND_ENUM_CONSTANT(VECTOR2);
    BIND_ENUM_CONSTANT(VECTOR3);
    BIND_ENUM_CONSTANT(VECTOR4);
    BIND_ENUM_CONSTANT(COLOR);
    BIND_ENUM_CONSTANT(QUATERNION);
    BIND_ENUM_CONSTANT(ENTITY);
    BIND_ENUM_CONSTANT(VARIANT);
}


} // namespace netw
