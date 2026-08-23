#include "netw/table/schema_record.hpp"

#include "godot/class_db.hpp"
#include "netw/api/schema_core.hpp"

using namespace godot;

namespace netw {

Ref<SchemaColumn> SchemaColumn::create(
    const StringName &key,
    int type,
    int stride
) {
    Ref<SchemaColumn> column;
    column.instantiate();
    column->key = key;
    column->type = type;
    column->stride = stride;
    return column;
}

void SchemaColumn::set_key(const StringName &value) {
    key = value;
}

StringName SchemaColumn::get_key() const {
    return key;
}

void SchemaColumn::set_type(int value) {
    type = value;
}

int SchemaColumn::get_type() const {
    return type;
}

void SchemaColumn::set_stride(int value) {
    stride = value;
}

int SchemaColumn::get_stride() const {
    return stride;
}

void SchemaColumn::set_quantizer(const Ref<NetwQuantize> &value) {
    quantizer = value;
}

Ref<NetwQuantize> SchemaColumn::get_quantizer() const {
    return quantizer;
}

void SchemaColumn::_bind_methods() {
    ClassDB::bind_static_method(
        "SchemaColumn",
        D_METHOD("create", "key", "type", "stride"),
        &SchemaColumn::create,
        DEFVAL(1)
    );
    ClassDB::bind_method(D_METHOD("set_key", "value"), &SchemaColumn::set_key);
    ClassDB::bind_method(D_METHOD("get_key"), &SchemaColumn::get_key);
    ClassDB::bind_method(
        D_METHOD("set_type", "value"),
        &SchemaColumn::set_type
    );
    ClassDB::bind_method(D_METHOD("get_type"), &SchemaColumn::get_type);
    ClassDB::bind_method(
        D_METHOD("set_stride", "value"),
        &SchemaColumn::set_stride
    );
    ClassDB::bind_method(D_METHOD("get_stride"), &SchemaColumn::get_stride);
    ClassDB::bind_method(
        D_METHOD("set_quantizer", "value"),
        &SchemaColumn::set_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_quantizer"),
        &SchemaColumn::get_quantizer
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "key"),
        "set_key",
        "get_key"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "type"), "set_type", "get_type");
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "stride"),
        "set_stride",
        "get_stride"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_quantizer",
        "get_quantizer"
    );
}

Ref<SchemaColumn> SchemaRecord::at(int column) const {
    if (column < 0 || column >= columns.size()) {
        return Ref<SchemaColumn>();
    }
    return Ref<SchemaColumn>(Object::cast_to<SchemaColumn>(columns[column]));
}

int SchemaRecord::column_count() const {
    return static_cast<int>(columns.size());
}

void SchemaRecord::set_name(const StringName &value) {
    name = value;
}

StringName SchemaRecord::get_name() const {
    return name;
}

void SchemaRecord::set_columns(const TypedArray<SchemaColumn> &value) {
    columns = value;
}

TypedArray<SchemaColumn> SchemaRecord::get_columns() const {
    return columns;
}

void SchemaRecord::set_sealed(bool value) {
    sealed = value;
}

bool SchemaRecord::get_sealed() const {
    return sealed;
}

void SchemaRecord::set_shape_hash(int value) {
    shape_hash = value;
}

int SchemaRecord::get_shape_hash() const {
    return shape_hash;
}

void SchemaRecord::_bind_methods() {
    ClassDB::bind_method(D_METHOD("at", "column"), &SchemaRecord::at);
    ClassDB::bind_method(D_METHOD("column_count"), &SchemaRecord::column_count);
    ClassDB::bind_method(
        D_METHOD("set_name", "value"),
        &SchemaRecord::set_name
    );
    ClassDB::bind_method(D_METHOD("get_name"), &SchemaRecord::get_name);
    ClassDB::bind_method(
        D_METHOD("set_columns", "value"),
        &SchemaRecord::set_columns
    );
    ClassDB::bind_method(D_METHOD("get_columns"), &SchemaRecord::get_columns);
    ClassDB::bind_method(
        D_METHOD("set_sealed", "value"),
        &SchemaRecord::set_sealed
    );
    ClassDB::bind_method(D_METHOD("get_sealed"), &SchemaRecord::get_sealed);
    ClassDB::bind_method(
        D_METHOD("set_shape_hash", "value"),
        &SchemaRecord::set_shape_hash
    );
    ClassDB::bind_method(
        D_METHOD("get_shape_hash"),
        &SchemaRecord::get_shape_hash
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "name"),
        "set_name",
        "get_name"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "columns",
            PROPERTY_HINT_ARRAY_TYPE,
            "SchemaColumn"
        ),
        "set_columns",
        "get_columns"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "sealed"),
        "set_sealed",
        "get_sealed"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "shape_hash"),
        "set_shape_hash",
        "get_shape_hash"
    );
}

} // namespace netw
