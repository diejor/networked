#include "netw/schema_core.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
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
    SchemaRecord *existing = record_of(schema);
    if (existing != nullptr) {
        open_redeclare(existing);
        return;
    }
    SchemaRecord record;
    record.name = name;
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
    SchemaRecord *record = record_of(schema);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return fix(record);
}

SchemaRecord *SchemaCore::record_of(const RID &schema) {
    HashMap<RID, SchemaRecord>::Iterator found = schemas.find(schema);
    return found != schemas.end() ? &found->value : nullptr;
}

const SchemaRecord *SchemaCore::record_of(const RID &schema) const {
    const HashMap<RID, SchemaRecord>::ConstIterator found
        = schemas.find(schema);
    return found != schemas.end() ? &found->value : nullptr;
}

void SchemaCore::sealed_records(
    LocalVector<const SchemaRecord *> &r_out
) const {
    for (const KeyValue<RID, SchemaRecord> &entry : schemas) {
        if (entry.value.sealed) {
            r_out.push_back(&entry.value);
        }
    }
}

bool SchemaCore::is_valid(const RID &schema) const {
    return schemas.has(schema);
}

RID SchemaCore::find(const StringName &name) const {
    const HashMap<StringName, RID>::ConstIterator found = by_name.find(name);
    return found != by_name.end() ? found->value : RID();
}

StringName SchemaCore::name_of(const RID &schema) const {
    const SchemaRecord *record = record_of(schema);
    return record != nullptr ? record->name : StringName();
}

int SchemaCore::hash_of(const RID &schema) const {
    const SchemaRecord *record = record_of(schema);
    return record != nullptr ? record->shape_hash : 0;
}

int SchemaCore::column_count(const RID &schema) const {
    const SchemaRecord *record = record_of(schema);
    return record != nullptr ? record->column_count() : 0;
}

StringName SchemaCore::column_key(const RID &schema, int column) const {
    const SchemaColumn *found = column_at(record_of(schema), column);
    return found != nullptr ? found->key : StringName();
}

int SchemaCore::column_type(const RID &schema, int column) const {
    const SchemaColumn *found = column_at(record_of(schema), column);
    return found != nullptr ? found->type : -1;
}

int SchemaCore::column_stride(const RID &schema, int column) const {
    const SchemaColumn *found = column_at(record_of(schema), column);
    return found != nullptr ? found->stride : 0;
}

Ref<NetwQuantize> SchemaCore::column_quantizer(
    const RID &schema,
    int column
) const {
    const SchemaColumn *found = column_at(record_of(schema), column);
    return found != nullptr ? found->quantizer : Ref<NetwQuantize>();
}

int SchemaCore::find_column(const RID &schema, const StringName &key) const {
    const SchemaRecord *record = record_of(schema);
    if (record == nullptr) {
        return -1;
    }
    for (int i = 0; i < record->column_count(); i++) {
        if (record->at(i)->key == key) {
            return i;
        }
    }
    return -1;
}

bool SchemaCore::has_variant(const RID &schema) const {
    const SchemaRecord *record = record_of(schema);
    if (record == nullptr) {
        return false;
    }
    for (int i = 0; i < record->column_count(); i++) {
        if (record->at(i)->type == VARIANT) {
            return true;
        }
    }
    return false;
}

bool SchemaCore::has_stride(const RID &schema) const {
    const SchemaRecord *record = record_of(schema);
    if (record == nullptr) {
        return false;
    }
    for (int i = 0; i < record->column_count(); i++) {
        if (record->at(i)->stride > 1) {
            return true;
        }
    }
    return false;
}

void SchemaCore::open_redeclare(SchemaRecord *record) {
    if (record == nullptr) {
        return;
    }
    record->redeclare_at = 0;
    record->redeclare_open = record->sealed;
    record->redeclare_failed = false;
}

int SchemaCore::append_column(
    SchemaRecord *record,
    const StringName &key,
    int type,
    int stride
) {
    if (record == nullptr || key == StringName() || stride < 1) {
        return -1;
    }
    if (!in_range(type)) {
        return -1;
    }
    if (record->sealed) {
        return match_redeclared(record, key, type, stride);
    }
    for (int i = 0; i < record->column_count(); i++) {
        if (record->at(i)->key == key) {
            return -1;
        }
    }
    SchemaColumn made;
    made.key = key;
    made.type = type;
    made.stride = stride;
    record->columns.push_back(made);
    return record->column_count() - 1;
}

void SchemaCore::assign_quantizer(
    SchemaRecord *record,
    int column,
    const Ref<NetwQuantize> &quantizer
) {
    if (record == nullptr || record->sealed) {
        return;
    }
    SchemaColumn *found = record->at(column);
    if (found == nullptr) {
        return;
    }
    found->quantizer = quantizer;
}

Error SchemaCore::fix(SchemaRecord *record) {
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (record->column_count() > MAX_COLUMNS) {
        NETW_ERR_V(
            ERR_INVALID_DECLARATION,
            sys::TABLE,
            "Schema %s declares %d columns and a row mask carries %d.",
            String(record->name).utf8().get_data(),
            record->column_count(),
            MAX_COLUMNS
        );
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

const SchemaColumn *SchemaCore::column_at(
    const SchemaRecord *record,
    int column
) {
    return record != nullptr ? record->at(column) : nullptr;
}

int SchemaCore::match_redeclared(
    SchemaRecord *record,
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
    const SchemaColumn *column = record->at(at);
    if (column->key != key || column->type != type
        || column->stride != stride) {
        record->redeclare_failed = true;
        return -1;
    }
    record->redeclare_at = at + 1;
    return at;
}

int SchemaCore::compute_hash(const SchemaRecord *record) {
    if (record == nullptr) {
        return 0;
    }
    PackedStringArray parts;
    parts.push_back(String(record->name));
    for (int i = 0; i < record->column_count(); i++) {
        const SchemaColumn *column = record->at(i);
        parts.push_back(
            String(column->key) + ":" + String::num_int64(column->type) + ":"
            + String::num_int64(column->stride) + ":" + quantizer_tag(column)
        );
    }
    const uint32_t folded
        = static_cast<uint32_t>(String("|").join(parts).hash());
    return static_cast<int>(folded & 0xFFFFu);
}

String SchemaCore::quantizer_tag(const SchemaColumn *column) {
    if (column == nullptr || column->quantizer.is_null()) {
        return "raw";
    }
    const int element = element_type(column->type);
    return column->quantizer->get_class() + "/"
        + String::num_int64(column->quantizer->total_bits(
            static_cast<Variant::Type>(element)
        ));
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

} // namespace netw
